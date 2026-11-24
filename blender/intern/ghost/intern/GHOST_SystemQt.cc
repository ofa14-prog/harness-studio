/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

#include "GHOST_SystemQt.hh"
#include "GHOST_ContextQt.hh"
#include "GHOST_EventManager.hh"
#include "GHOST_QtSurface.hh"
#include "GHOST_TimerManager.hh"
#include "GHOST_WindowManager.hh"
#include "GHOST_WindowQt.hh"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <cstdio>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QScreen>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>

#include <clocale>
#include <cstring>

static int g_argc = 1;
static char g_arg0[] = "harness-studio";
static char *g_argv[] = {g_arg0, nullptr};

/**
 * Hybrid-graphics laptops start programs on the integrated GPU. Measured on an Intel UHD
 * (ADL-S GT0.5) + RTX 4050 laptop with a continuously orbiting viewport: 28-45 frames/s on the
 * integrated GPU, 208 frames/s (every 144 Hz refresh shown) on the RTX. So the discrete GPU is
 * chosen before any GL library picks a vendor, unless the environment already chose one or
 * HARNESS_GPU=integrated.
 */
static void harness_prefer_discrete_gpu()
{
  if (qgetenv("HARNESS_GPU") == "integrated") {
    return;
  }
  if (qEnvironmentVariableIsSet("__NV_PRIME_RENDER_OFFLOAD") ||
      qEnvironmentVariableIsSet("__GLX_VENDOR_LIBRARY_NAME") ||
      qEnvironmentVariableIsSet("DRI_PRIME"))
  {
    return;
  }
  if (QFileInfo::exists(QStringLiteral("/proc/driver/nvidia/version"))) {
    qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
    qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
    qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
    const char *egl = "/usr/share/glvnd/egl_vendor.d/10_nvidia.json";
    if (qEnvironmentVariableIsEmpty("__EGL_VENDOR_LIBRARY_FILENAMES") &&
        QFileInfo::exists(QString::fromLatin1(egl)))
    {
      qputenv("__EGL_VENDOR_LIBRARY_FILENAMES", egl);
    }
    return;
  }
  /* Other hybrid systems (Intel + AMD, AMD + AMD): Mesa renders on the other GPU. */
  const QStringList nodes = QDir(QStringLiteral("/dev/dri"))
                                .entryList({QStringLiteral("renderD*")}, QDir::System);
  if (nodes.size() >= 2) {
    qputenv("DRI_PRIME", "1");
  }
}

GHOST_SystemQt::GHOST_SystemQt()
{
  harness_prefer_discrete_gpu();
  /* Blender's GL contexts and the Qt Quick scene graph share one thread and one share group:
   * the basic render loop renders inside #processEvents, between Blender's own draws. */
  qputenv("QSG_RENDER_LOOP", "basic");
  /* Show a new Blender frame on the next pass of the loop instead of after a 5 ms idle timer. */
  if (qEnvironmentVariableIsEmpty("QT_QPA_UPDATE_IDLE_TIME")) {
    qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
  }
  if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  }
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  /* Blender makes its contexts current on job threads too. */
  QCoreApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity);
  /* Text sharpness: render at the real device scale without rounding the factor, so glyphs
   * are rasterised for the physical pixel grid instead of being resampled. */
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setVersion(4, 3);
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  format.setAlphaBufferSize(8);
  /* Never wait for the display in the swap: Qt Quick renders on Blender's main thread, so a
   * blocking (v-synced) swap stalled Blender's loop for a whole refresh every frame and every
   * other Blender frame was never shown — the viewport stutter. Qt still paces its frames by the
   * compositor (frame callbacks / update requests) and always shows Blender's newest frame. */
  format.setSwapInterval(0);
  QSurfaceFormat::setDefaultFormat(format);

  if (!QGuiApplication::instance()) {
    new QGuiApplication(g_argc, g_argv);
  }
  /* Qt adopts the user's locale; Blender parses and prints numbers with the C locale
   * (a Turkish locale would turn "1.5" into "1,5" and break case folding of 'i'). */
  setlocale(LC_NUMERIC, "C");
  setlocale(LC_COLLATE, "C");
  QGuiApplication::setApplicationName("Harness Studio");
  QGuiApplication::setOrganizationName("HarnessStudio");
  QGuiApplication::setDesktopFileName("harness-studio");
  QGuiApplication::setQuitOnLastWindowClosed(false);
  clock_.start();

  /* Python is not started yet: the shell registers its built-in modules now. */
  harness_shell_preinit();
}

GHOST_SystemQt::~GHOST_SystemQt()
{
  harness_shell_shutdown();
}

GHOST_TSuccess GHOST_SystemQt::init()
{
  GHOST_TSuccess success = GHOST_System::init();
  if (success == GHOST_kSuccess && !shell_created_) {
    harness_shell_create();
    shell_created_ = true;
  }
  return success;
}

GHOST_IWindow *GHOST_SystemQt::createWindow(const char *title,
                                            int32_t left,
                                            int32_t top,
                                            uint32_t width,
                                            uint32_t height,
                                            GHOST_TWindowState state,
                                            GHOST_GPUSettings gpu_settings,
                                            const bool /*exclusive*/,
                                            const bool is_dialog,
                                            const GHOST_IWindow *parent_window)
{
  const GHOST_ContextParams context_params = GHOST_CONTEXT_PARAMS_FROM_GPU_SETTINGS(gpu_settings);
  GHOST_TDrawingContextType type = gpu_settings.context_type;
  if (type != GHOST_kDrawingContextTypeOpenGL && type != GHOST_kDrawingContextTypeNone) {
    /* The Qt shell composites OpenGL textures; other back-ends are not supported here. */
    return nullptr;
  }
  auto *window = new GHOST_WindowQt(this,
                                    title,
                                    left,
                                    top,
                                    width,
                                    height,
                                    state,
                                    type,
                                    context_params,
                                    is_dialog,
                                    parent_window);
  if (!window->getValid()) {
    delete window;
    return nullptr;
  }
  window_manager_->addWindow(window);
  window_manager_->setActiveWindow(window);
  GHOST_QtHost::instance()->attachWindow(window);
  pushEvent(std::make_unique<GHOST_Event>(getMilliSeconds(), GHOST_kEventWindowSize, window));
  return window;
}

void GHOST_SystemQt::windowClosed(GHOST_WindowQt *window)
{
  GHOST_QtHost::instance()->detachWindow(window);
}

GHOST_IWindow *GHOST_SystemQt::activeWindow() const
{
  return window_manager_->getActiveWindow();
}

void GHOST_SystemQt::queueEvent(std::unique_ptr<const GHOST_IEvent> event)
{
  if (event->getType() == GHOST_kEventWindowActivate) {
    window_manager_->setActiveWindow(const_cast<GHOST_IWindow *>(event->getWindow()));
  }
  else if (event->getType() == GHOST_kEventWindowDeactivate) {
    window_manager_->setWindowInactive(const_cast<GHOST_IWindow *>(event->getWindow()));
  }
  pushEvent(std::move(event));
}

/* HARNESS_PROFILE=1: once a second, print how the main loop's time splits between Blender
 * and Qt (event handling, Qt Quick rendering and the buffer swap). */
int g_harness_prof_blender_frames = 0;
int g_harness_prof_qt_frames = 0;
double g_harness_prof_swap_ms = 0.0;
static const bool g_harness_prof = qEnvironmentVariableIsSet("HARNESS_PROFILE");

bool GHOST_SystemQt::processEvents(bool waitForEvent)
{
  static QElapsedTimer prof_clock;
  static qint64 prof_last_exit = -1, prof_qt_ns = 0, prof_bl_ns = 0, prof_window_start = 0;
  static int prof_calls = 0;
  if (g_harness_prof) {
    if (!prof_clock.isValid()) {
      prof_clock.start();
    }
    if (prof_last_exit >= 0) {
      prof_bl_ns += prof_clock.nsecsElapsed() - prof_last_exit;
    }
  }
  const qint64 prof_enter = g_harness_prof ? prof_clock.nsecsElapsed() : 0;
  bool any = false;
  GHOST_TimerManager *timers = getTimerManager();
  /* Whatever context Blender had current must be current again after Qt Quick rendered. */
  GHOST_Context *ctx = GHOST_Context::getActiveDrawingContext();

  do {
    const size_t before = event_manager_->getNumEvents();
    if (waitForEvent && before == 0) {
      const uint64_t next = timers->nextFireTime();
      int sleep_ms = 8;
      if (next != GHOST_kFireTimeNever) {
        const int64_t until = int64_t(next) - int64_t(getMilliSeconds());
        sleep_ms = int(std::clamp<int64_t>(until, 0, 8));
      }
      QCoreApplication::processEvents(QEventLoop::AllEvents, sleep_ms);
      if (event_manager_->getNumEvents() == 0 && sleep_ms > 0) {
        QThread::msleep(1);
      }
    }
    else {
      QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    if (timers->fireTimers(getMilliSeconds())) {
      any = true;
    }
    if (event_manager_->getNumEvents() > before) {
      any = true;
    }
  } while (waitForEvent && !any);

  if (ctx && GHOST_Context::getActiveDrawingContext() == ctx) {
    static_cast<GHOST_ContextQt *>(ctx)->restoreCurrent();
  }
  if (g_harness_prof) {
    const qint64 now = prof_clock.nsecsElapsed();
    prof_qt_ns += now - prof_enter;
    prof_last_exit = now;
    prof_calls++;
    if (now - prof_window_start > 1000000000LL) {
      fprintf(stderr,
              "harness-profile: blender frames %d, shown %d, loop %d, blender %.1f ms, qt %.1f ms, swap %.1f ms per s\n",
              g_harness_prof_blender_frames,
              g_harness_prof_qt_frames,
              prof_calls,
              prof_bl_ns / 1e6,
              prof_qt_ns / 1e6,
              g_harness_prof_swap_ms);
      g_harness_prof_swap_ms = 0.0;
      g_harness_prof_blender_frames = 0;
      g_harness_prof_qt_frames = 0;
      prof_calls = 0;
      prof_bl_ns = prof_qt_ns = 0;
      prof_window_start = now;
    }
  }
  return any;
}

GHOST_TSuccess GHOST_SystemQt::getModifierKeys(GHOST_ModifierKeys &keys) const
{
  const Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
  keys.set(GHOST_kModifierKeyLeftShift, mods & Qt::ShiftModifier);
  keys.set(GHOST_kModifierKeyRightShift, false);
  keys.set(GHOST_kModifierKeyLeftControl, mods & Qt::ControlModifier);
  keys.set(GHOST_kModifierKeyRightControl, false);
  keys.set(GHOST_kModifierKeyLeftAlt, mods & Qt::AltModifier);
  keys.set(GHOST_kModifierKeyRightAlt, false);
  keys.set(GHOST_kModifierKeyLeftOS, mods & Qt::MetaModifier);
  keys.set(GHOST_kModifierKeyRightOS, false);
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_SystemQt::getButtons(GHOST_Buttons &buttons) const
{
  const Qt::MouseButtons b = QGuiApplication::mouseButtons();
  buttons.set(GHOST_kButtonMaskLeft, b & Qt::LeftButton);
  buttons.set(GHOST_kButtonMaskMiddle, b & Qt::MiddleButton);
  buttons.set(GHOST_kButtonMaskRight, b & Qt::RightButton);
  buttons.set(GHOST_kButtonMaskButton4, b & Qt::BackButton);
  buttons.set(GHOST_kButtonMaskButton5, b & Qt::ForwardButton);
  return GHOST_kSuccess;
}

GHOST_TCapabilityFlag GHOST_SystemQt::getCapabilities() const
{
  return GHOST_TCapabilityFlag(GHOST_CAPABILITY_FLAG_ALL &
                               ~(GHOST_kCapabilityCursorWarp | GHOST_kCapabilityWindowPosition |
                                 GHOST_kCapabilityClipboardPrimary |
                                 GHOST_kCapabilityGPUReadFrontBuffer |
                                 GHOST_kCapabilityClipboardImage |
                                 GHOST_kCapabilityDesktopSample | GHOST_kCapabilityInputIME |
                                 GHOST_kCapabilityTrackpadPhysicalDirection |
                                 GHOST_kCapabilityWindowDecorationStyles |
                                 GHOST_kCapabilityKeyboardHyperKey |
                                 GHOST_kCapabilityCursorRGBA | GHOST_kCapabilityCursorGenerator |
                                 GHOST_kCapabilityMultiMonitorPlacement |
                                 GHOST_kCapabilityWindowPath));
  /* Window decoration is the shell's (server side from Blender's point of view): Blender must
   * not draw its own title bar inside the window. */
}

char *GHOST_SystemQt::getClipboard(bool selection) const
{
  const QByteArray text = QGuiApplication::clipboard()
                              ->text(selection ? QClipboard::Selection : QClipboard::Clipboard)
                              .toUtf8();
  char *result = static_cast<char *>(malloc(size_t(text.size()) + 1));
  memcpy(result, text.constData(), size_t(text.size()));
  result[text.size()] = '\0';
  return result;
}

void GHOST_SystemQt::putClipboard(const char *buffer, bool selection) const
{
  QGuiApplication::clipboard()->setText(QString::fromUtf8(buffer),
                                        selection ? QClipboard::Selection : QClipboard::Clipboard);
}

uint64_t GHOST_SystemQt::getMilliSeconds() const
{
  return uint64_t(clock_.elapsed());
}

uint8_t GHOST_SystemQt::getNumDisplays() const
{
  return uint8_t(std::max<qsizetype>(1, QGuiApplication::screens().size()));
}

GHOST_TSuccess GHOST_SystemQt::getCursorPosition(int32_t &x, int32_t &y) const
{
  x = cursor_x_;
  y = cursor_y_;
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_SystemQt::setCursorPosition(int32_t x, int32_t y)
{
  /* Pointer warping is not available on every platform Qt runs on (Wayland); only track it. */
  cursor_x_ = x;
  cursor_y_ = y;
  return GHOST_kSuccess;
}

void GHOST_SystemQt::getAllDisplayDimensions(uint32_t &width, uint32_t &height) const
{
  QRect all;
  for (QScreen *s : QGuiApplication::screens()) {
    all |= QRect(s->geometry().topLeft(), s->geometry().size() * s->devicePixelRatio());
  }
  width = uint32_t(std::max(1, all.width()));
  height = uint32_t(std::max(1, all.height()));
}

void GHOST_SystemQt::getMainDisplayDimensions(uint32_t &width, uint32_t &height) const
{
  QScreen *s = QGuiApplication::primaryScreen();
  const QSize size = s ? s->size() * s->devicePixelRatio() : QSize(1920, 1080);
  width = uint32_t(size.width());
  height = uint32_t(size.height());
}

GHOST_IContext *GHOST_SystemQt::createOffscreenContext(GHOST_GPUSettings gpu_settings)
{
  if (gpu_settings.context_type != GHOST_kDrawingContextTypeOpenGL) {
    return nullptr;
  }
  const GHOST_ContextParams params = GHOST_CONTEXT_PARAMS_FROM_GPU_SETTINGS_OFFSCREEN(
      gpu_settings);
  auto *context = new GHOST_ContextQt(params, nullptr);
  if (context->initializeDrawingContext() == GHOST_kSuccess) {
    return context;
  }
  delete context;
  return nullptr;
}

GHOST_TSuccess GHOST_SystemQt::disposeContext(GHOST_IContext *context)
{
  delete context;
  return GHOST_kSuccess;
}
