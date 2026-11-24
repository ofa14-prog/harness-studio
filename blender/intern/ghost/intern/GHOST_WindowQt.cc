/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

#include "GHOST_WindowQt.hh"
#include "GHOST_ContextNone.hh"
#include "GHOST_ContextQt.hh"
#include "GHOST_QtSurface.hh"
#include "GHOST_SystemQt.hh"

#include <QBitmap>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>

static int64_t g_window_counter = 0;

GHOST_WindowQt::GHOST_WindowQt(GHOST_SystemQt *system,
                               const char *title,
                               int32_t left,
                               int32_t top,
                               uint32_t width,
                               uint32_t height,
                               GHOST_TWindowState state,
                               GHOST_TDrawingContextType type,
                               const GHOST_ContextParams &context_params,
                               bool is_dialog,
                               const GHOST_IWindow *parent_window)
    : GHOST_Window(width, height, state, context_params, false),
      system_(system),
      title_(title ? title : ""),
      x_(left),
      y_(top),
      width_(int(std::max<uint32_t>(width, 1))),
      height_(int(std::max<uint32_t>(height, 1))),
      state_(state),
      is_dialog_(is_dialog),
      id_(++g_window_counter),
      parent_(parent_window)
{
  QScreen *screen = QGuiApplication::primaryScreen();
  dpr_ = screen ? float(screen->devicePixelRatio()) : 1.0f;
  native_pixel_size_ = 1.0f;
  setDrawingContextType(type);
}

GHOST_WindowQt::~GHOST_WindowQt()
{
  system_->windowClosed(this);
  releaseNativeHandles();
  delete static_cast<QCursor *>(custom_cursor_);
}

GHOST_Context *GHOST_WindowQt::newDrawingContext(GHOST_TDrawingContextType type)
{
  if (type == GHOST_kDrawingContextTypeNone) {
    return new GHOST_ContextNone(want_context_params_);
  }
  if (type != GHOST_kDrawingContextTypeOpenGL) {
    return nullptr;
  }
  auto *context = new GHOST_ContextQt(want_context_params_, this);
  if (context->initializeDrawingContext() == GHOST_kSuccess) {
    return context;
  }
  delete context;
  return nullptr;
}

GHOST_ContextQt *GHOST_WindowQt::qtContext() const
{
  return dynamic_cast<GHOST_ContextQt *>(
      const_cast<GHOST_WindowQt *>(this)->getDrawingContext());
}

bool GHOST_WindowQt::getValid() const
{
  return GHOST_Window::getValid();
}

void GHOST_WindowQt::setTitle(const char *title)
{
  title_ = title ? title : "";
  if (surface_) {
    surface_->titleChanged();
  }
  if (surface_ && surface_->slot() == QLatin1String("center")) {
    Q_EMIT GHOST_QtHost::instance()->mainTitleChanged(QString::fromStdString(title_), modified_);
  }
}

std::string GHOST_WindowQt::getTitle() const
{
  return title_;
}

GHOST_TSuccess GHOST_WindowQt::setModifiedState(bool is_unsaved_changes)
{
  modified_ = is_unsaved_changes;
  if (surface_ && surface_->slot() == QLatin1String("center")) {
    Q_EMIT GHOST_QtHost::instance()->mainTitleChanged(QString::fromStdString(title_), modified_);
  }
  return GHOST_Window::setModifiedState(is_unsaved_changes);
}

void GHOST_WindowQt::getWindowBounds(GHOST_Rect &bounds) const
{
  getClientBounds(bounds);
}

void GHOST_WindowQt::getClientBounds(GHOST_Rect &bounds) const
{
  bounds.l_ = x_;
  bounds.t_ = y_;
  bounds.r_ = x_ + width_;
  bounds.b_ = y_ + height_;
}

GHOST_TSuccess GHOST_WindowQt::setClientWidth(uint32_t /*width*/)
{
  /* The shell's layout decides the size. */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::setClientHeight(uint32_t /*height*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::setClientSize(uint32_t /*width*/, uint32_t /*height*/)
{
  return GHOST_kSuccess;
}

void GHOST_WindowQt::screenToClient(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const
{
  outX = inX - x_;
  outY = inY - y_;
}

void GHOST_WindowQt::clientToScreen(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const
{
  outX = inX + x_;
  outY = inY + y_;
}

GHOST_TWindowState GHOST_WindowQt::getState() const
{
  return state_;
}

GHOST_TSuccess GHOST_WindowQt::setState(GHOST_TWindowState state)
{
  state_ = state;
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::setOrder(GHOST_TWindowOrder /*order*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::invalidate()
{
  system_->queueEvent(std::make_unique<GHOST_Event>(
      system_->getMilliSeconds(), GHOST_kEventWindowUpdate, this));
  return GHOST_kSuccess;
}

uint16_t GHOST_WindowQt::getDPIHint()
{
  /* Device pixels are reported as window pixels; the DPI hint carries the display scale so
   * Blender's interface keeps its physical size. */
  return uint16_t(std::lround(96.0f * dpr_));
}

void GHOST_WindowQt::attachSurface(GHOST_QtSurface *surface)
{
  surface_ = surface;
  has_frame_ = false;
  applyCursor();
}

void GHOST_WindowQt::detachSurface(GHOST_QtSurface *surface)
{
  if (surface_ == surface) {
    surface_ = nullptr;
  }
}

void GHOST_WindowQt::setSurfaceGeometry(int x, int y, int w, int h, float dpr)
{
  w = std::max(1, w);
  h = std::max(1, h);
  const bool resized = (w != width_ || h != height_ || dpr != dpr_);
  const bool moved = (x != x_ || y != y_);
  x_ = x;
  y_ = y;
  width_ = w;
  height_ = h;
  if (dpr != dpr_) {
    dpr_ = dpr;
    system_->queueEvent(std::make_unique<GHOST_Event>(
        system_->getMilliSeconds(), GHOST_kEventWindowDPIHintChanged, this));
  }
  if (resized) {
    system_->queueEvent(std::make_unique<GHOST_Event>(
        system_->getMilliSeconds(), GHOST_kEventWindowSize, this));
  }
  else if (moved) {
    system_->queueEvent(std::make_unique<GHOST_Event>(
        system_->getMilliSeconds(), GHOST_kEventWindowMove, this));
  }
}

void GHOST_WindowQt::requestFullRedraw()
{
  system_->queueEvent(std::make_unique<GHOST_Event>(
      system_->getMilliSeconds(), GHOST_kEventWindowUpdate, this));
}

void GHOST_WindowQt::frameReady()
{
  has_frame_ = true;
  if (surface_) {
    surface_->frameReady();
  }
}

static Qt::CursorShape qt_cursor_for(GHOST_TStandardCursor shape)
{
  switch (shape) {
    case GHOST_kStandardCursorRightArrow:
    case GHOST_kStandardCursorLeftArrow:
    case GHOST_kStandardCursorDefault:
      return Qt::ArrowCursor;
    case GHOST_kStandardCursorInfo:
    case GHOST_kStandardCursorHelp:
      return Qt::WhatsThisCursor;
    case GHOST_kStandardCursorDestroy:
    case GHOST_kStandardCursorStop:
      return Qt::ForbiddenCursor;
    case GHOST_kStandardCursorWait:
      return Qt::WaitCursor;
    case GHOST_kStandardCursorText:
      return Qt::IBeamCursor;
    case GHOST_kStandardCursorCrosshair:
    case GHOST_kStandardCursorCrosshairA:
    case GHOST_kStandardCursorCrosshairB:
    case GHOST_kStandardCursorCrosshairC:
    case GHOST_kStandardCursorPencil:
    case GHOST_kStandardCursorEraser:
    case GHOST_kStandardCursorKnife:
    case GHOST_kStandardCursorEyedropper:
    case GHOST_kStandardCursorBlade:
      return Qt::CrossCursor;
    case GHOST_kStandardCursorUpArrow:
      return Qt::UpArrowCursor;
    case GHOST_kStandardCursorDownArrow:
      return Qt::ArrowCursor;
    case GHOST_kStandardCursorVerticalSplit:
    case GHOST_kStandardCursorLeftRight:
    case GHOST_kStandardCursorEWScroll:
    case GHOST_kStandardCursorLeftSide:
    case GHOST_kStandardCursorRightSide:
    case GHOST_kStandardCursorSlip:
      return Qt::SizeHorCursor;
    case GHOST_kStandardCursorHorizontalSplit:
    case GHOST_kStandardCursorUpDown:
    case GHOST_kStandardCursorNSScroll:
    case GHOST_kStandardCursorTopSide:
    case GHOST_kStandardCursorBottomSide:
      return Qt::SizeVerCursor;
    case GHOST_kStandardCursorTopLeftCorner:
    case GHOST_kStandardCursorBottomRightCorner:
      return Qt::SizeFDiagCursor;
    case GHOST_kStandardCursorTopRightCorner:
    case GHOST_kStandardCursorBottomLeftCorner:
      return Qt::SizeBDiagCursor;
    case GHOST_kStandardCursorZoomIn:
    case GHOST_kStandardCursorZoomOut:
      return Qt::CrossCursor;
    case GHOST_kStandardCursorMove:
    case GHOST_kStandardCursorNSEWScroll:
      return Qt::SizeAllCursor;
    case GHOST_kStandardCursorCopy:
      return Qt::DragCopyCursor;
    case GHOST_kStandardCursorHandOpen:
      return Qt::OpenHandCursor;
    case GHOST_kStandardCursorHandClosed:
      return Qt::ClosedHandCursor;
    case GHOST_kStandardCursorHandPoint:
      return Qt::PointingHandCursor;
    case GHOST_kStandardCursorLeftHandle:
    case GHOST_kStandardCursorRightHandle:
    case GHOST_kStandardCursorBothHandles:
      return Qt::SizeHorCursor;
    default:
      return Qt::ArrowCursor;
  }
}

void GHOST_WindowQt::applyCursor()
{
  if (!surface_) {
    return;
  }
  if (!cursor_visible_) {
    surface_->applyCursor(QCursor(Qt::BlankCursor));
  }
  else if (cursor_custom_ && custom_cursor_) {
    surface_->applyCursor(*static_cast<QCursor *>(custom_cursor_));
  }
  else {
    surface_->applyCursor(QCursor(Qt::CursorShape(cursor_shape_qt_)));
  }
}

GHOST_TSuccess GHOST_WindowQt::setWindowCursorGrab(GHOST_TGrabCursorMode /*mode*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::setWindowCursorShape(GHOST_TStandardCursor shape)
{
  cursor_custom_ = false;
  cursor_shape_qt_ = int(qt_cursor_for(shape));
  applyCursor();
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::hasCursorShape(GHOST_TStandardCursor shape)
{
  return shape == GHOST_kStandardCursorCustom ? GHOST_kFailure : GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::setWindowCustomCursorShape(const uint8_t *bitmap,
                                                          const uint8_t *mask,
                                                          const int size[2],
                                                          const int hot_spot[2],
                                                          bool /*can_invert_color*/)
{
  /* 1-bit bitmap + mask, rows padded to bytes, least significant bit first. */
  const int w = size[0], h = size[1];
  const int stride = (w + 7) / 8;
  QImage img(w, h, QImage::Format_ARGB32);
  img.fill(Qt::transparent);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const int byte = y * stride + x / 8;
      const int bit = 1 << (x % 8);
      const bool on = bitmap[byte] & bit;
      const bool m = mask ? (mask[byte] & bit) : on;
      if (m) {
        img.setPixel(x, y, on ? 0xFFFFFFFF : 0xFF000000);
      }
    }
  }
  delete static_cast<QCursor *>(custom_cursor_);
  custom_cursor_ = new QCursor(QPixmap::fromImage(img), hot_spot[0], hot_spot[1]);
  cursor_custom_ = true;
  applyCursor();
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowQt::setWindowCursorVisibility(bool visible)
{
  cursor_visible_ = visible;
  applyCursor();
  return GHOST_kSuccess;
}
