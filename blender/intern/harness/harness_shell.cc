/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 */

#include "harness_shell.hh"
#include "gltf_model.hh"
#include "harness_bridge.hh"
#include "studio/studio.hh"
#include "studio/profiles.hh"

#include "GHOST_QtSurface.hh"

#include <cstdio>
#include <QClipboard>
#include <QImage>
#include <QDateTime>
#include <QStandardPaths>
#include <QImageReader>
#include <QMimeData>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#include <cstdlib>

static void harness_init_resources()
{
  Q_INIT_RESOURCE(harness);
}

namespace harness {

Shell *Shell::instance()
{
  static Shell *shell = new Shell();
  return shell;
}

static void setup_fonts()
{
  /* The brand type: Instrument Sans (interface), Cabin (secondary), Notable (hero). */
  const QDir fonts(QStringLiteral(":/harness/fonts"));
  for (const QString &f : fonts.entryList({QStringLiteral("*.ttf")}, QDir::Files)) {
    if (f.contains(QLatin1String("Variable"))) {
      continue; /* For Blender's own text (weights by axis); Qt uses the static cuts. */
    }
    QFontDatabase::addApplicationFont(fonts.filePath(f));
  }
  /* Sharp text: glyphs are rasterised natively for the device pixel grid with full hinting
   * (no distance-field scaling, no fractional resampling of text textures). */
  QFont font(QStringLiteral("Instrument Sans"));
  font.setPixelSize(13);
  font.setHintingPreference(QFont::PreferFullHinting);
  font.setStyleStrategy(QFont::PreferAntialias);
  QGuiApplication::setFont(font);
  QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
}

void Shell::setupEngine(QQmlEngine *engine)
{
  engine->addImportPath(QStringLiteral("qrc:/harness/qml"));
  QQmlContext *ctx = engine->rootContext();
  ctx->setContextProperty(QStringLiteral("shell"), this);
  ctx->setContextProperty(QStringLiteral("blender"), Bridge::instance());
  ctx->setContextProperty(QStringLiteral("studio"), Studio::instance());
  ctx->setContextProperty(QStringLiteral("profiles"), Profiles::instance());
}

void Shell::create()
{
  harness_init_resources();
  setup_fonts();
  qmlRegisterType<GHOST_QtSurface>("Harness.Blender", 1, 0, "BlenderSurface");
  qmlRegisterType<harness::GltfModel>("Harness.Gltf", 1, 0, "GltfModel");
  qmlRegisterAnonymousType<harness::GltfGeometry>("Harness.Gltf", 1);

  Profiles::instance()->load();
  Studio::instance()->attach(Profiles::instance());

  view_ = new QQuickView();
  view_->setTitle(QStringLiteral("Harness Studio"));
  view_->setFlags(Qt::Window | Qt::FramelessWindowHint);
  if (!qEnvironmentVariableIsEmpty("HARNESS_SCREENSHOT")) {
    /* Automated pictures must not take focus from what the person is doing. */
    view_->setFlag(Qt::WindowDoesNotAcceptFocus, true);
  }
  view_->setColor(QColor(QStringLiteral("#0e0f11")));
  view_->setResizeMode(QQuickView::SizeRootObjectToView);
  view_->setMinimumSize(QSize(1100, 680));
  setupEngine(view_->engine());
  view_->setSource(QUrl(QStringLiteral("qrc:/harness/qml/Main.qml")));
  if (view_->status() == QQuickView::Error) {
    for (const auto &e : view_->errors()) {
      fprintf(stderr, "harness: %s\n", qPrintable(e.toString()));
    }
  }
  connect(Bridge::instance(), &Bridge::stateChanged, this, [this]() {
    if (!view_) return;
    const QString name = Bridge::instance()->projectName();
    view_->setTitle(name.isEmpty() ? QStringLiteral("Harness Studio")
                                   : name + QStringLiteral(" — Harness Studio"));
  });
  QScreen *screen = QGuiApplication::primaryScreen();
  const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1600, 950);
  const QSize size(std::min(1680, avail.width()), std::min(1000, avail.height()));
  view_->resize(size);
  view_->setPosition(avail.center() - QPoint(size.width() / 2, size.height() / 2));
  connect(view_, &QWindow::windowStateChanged, this, &Shell::windowStateChanged);
  /* The device pixel ratio (fractional scaling) is known only after the compositor tells the
   * window its scale, and changes with the screen: let QML re-read it. */
  connect(view_, &QWindow::screenChanged, this, [this](QScreen *) { Q_EMIT windowStateChanged(); });
  for (int ms : {100, 600, 2000}) {
    QTimer::singleShot(ms, this, [this]() { Q_EMIT windowStateChanged(); });
  }
  view_->show();
  if (qEnvironmentVariableIsSet("HARNESS_DEBUG")) {
    auto report = [this]() {
      QScreen *sc = view_->screen();
      fprintf(stderr, "harness: window dpr %.3f, screen %s dpr %.3f, platform %s\n",
              view_->devicePixelRatio(), sc ? qPrintable(sc->name()) : "?",
              sc ? sc->devicePixelRatio() : 0.0, qPrintable(QGuiApplication::platformName()));
    };
    QTimer::singleShot(1500, this, report);
    connect(view_, &QWindow::screenChanged, this, [report](QScreen *) { report(); });
  }

  connect(GHOST_QtHost::instance(), &GHOST_QtHost::floatingWindowRequested, this,
          &Shell::createFloating);
  connect(GHOST_QtHost::instance(), &GHOST_QtHost::floatingWindowClosed, this,
          [this](const QString &slot) {
            auto it = floating_.find(slot);
            if (it != floating_.end()) {
              if (it->second) {
                it->second->deleteLater();
              }
              floating_.erase(it);
            }
          });
  scheduleScreenshot();
}

void Shell::createFloating(const QString &slot, const QString &title, int w, int h, bool dialog)
{
  auto *view = new QQuickView();
  view->setTitle(title.isEmpty() ? QStringLiteral("Harness Studio") : title);
  view->setColor(QColor(QStringLiteral("#0e0f11")));
  view->setResizeMode(QQuickView::SizeRootObjectToView);
  setupEngine(view->engine());
  view->setInitialProperties({{QStringLiteral("slot"), slot}});
  view->setSource(QUrl(QStringLiteral("qrc:/harness/qml/FloatingWindow.qml")));
  const qreal dpr = view->devicePixelRatio();
  view->resize(QSize(std::max(320, int(w / dpr)), std::max(240, int(h / dpr))));
  if (dialog && view_) {
    view->setTransientParent(view_);
  }
  /* Closing the native window asks Blender to close its window (it may refuse). */
  connect(view, &QQuickView::closing, view, [view](QQuickCloseEvent *) {
    if (auto *root = view->rootObject()) {
      QMetaObject::invokeMethod(root, "requestClose");
    }
  });
  view->show();
  floating_[slot] = view;
}

void Shell::shutdown()
{
  for (auto &[slot, v] : floating_) {
    delete v;
  }
  floating_.clear();
  delete view_;
  view_ = nullptr;
}

bool Shell::maximized() const
{
  return view_ && (view_->windowStates() & Qt::WindowMaximized);
}

qreal Shell::devicePixelRatio() const
{
  return view_ ? view_->devicePixelRatio() : 1.0;
}

void Shell::minimize()
{
  if (view_) view_->showMinimized();
}

void Shell::toggleMaximize()
{
  if (!view_) return;
  if (maximized()) view_->showNormal();
  else view_->showMaximized();
}

void Shell::close()
{
  /* Same as closing Blender's main window: Blender asks about unsaved changes and quits. */
  Bridge::instance()->runOperator(QStringLiteral("wm.quit_blender"));
}

void Shell::startMove()
{
  if (view_) view_->startSystemMove();
}

void Shell::startResize(int edges)
{
  if (view_) view_->startSystemResize(Qt::Edges(edges));
}

QString Shell::fileUrl(const QString &path) const
{
  return QUrl::fromLocalFile(path).toString();
}

QString Shell::clipboardImage() const
{
  const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
  if (!mime) return QString();
  if (mime->hasUrls()) {
    for (const QUrl &u : mime->urls()) {
      if (u.isLocalFile() && isImage(u.toLocalFile())) return u.toString();
    }
  }
  if (!mime->hasImage()) return QString();
  const QImage img = qvariant_cast<QImage>(mime->imageData());
  if (img.isNull()) return QString();
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                      QStringLiteral("/HarnessStudio/attachments");
  QDir().mkpath(dir);
  const QString path = dir + QStringLiteral("/paste-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
  return img.save(path, "PNG") ? QUrl::fromLocalFile(path).toString() : QString();
}

bool Shell::isImage(const QString &url) const
{
  const QUrl u(url);
  const QString path = u.isLocalFile() ? u.toLocalFile() : url;
  const QByteArray fmt = QImageReader::imageFormat(path);
  return !fmt.isEmpty();
}

void Shell::openExternally(const QString &url) const
{
  QDesktopServices::openUrl(QUrl(url));
}

void Shell::copyText(const QString &text) const
{
  QGuiApplication::clipboard()->setText(text);
}

void Shell::captureAndExit()
{
  const QString path = qEnvironmentVariable("HARNESS_SCREENSHOT");
  if (!path.isEmpty() && view_) {
    view_->update();
    QDir().mkpath(QFileInfo(path).absolutePath());
    view_->grabWindow().save(path);
  }
  fflush(stdout);
  fflush(stderr);
  std::_Exit(0);
}

void Shell::scheduleScreenshot()
{
  const QByteArray path = qgetenv("HARNESS_SCREENSHOT");
  if (path.isEmpty() || !view_) {
    return;
  }
  if (qEnvironmentVariableIsSet("HARNESS_AGENT_TEST")) {
    /* An agent test decides when to take the picture (captureAndExit), after its task. */
    QTimer::singleShot(900000, this, [this]() { captureAndExit(); });
    return;
  }
  /* Wait until Blender drew its window and the workspace layout settled, then more frames. */
  auto *frames = new int(0);
  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this, frames, timer, path]() {
    /* The Blender window placed and the bridge running. */
    const bool docked = GHOST_QtHost::instance()->windowInSlot(QStringLiteral("center")) &&
                        Bridge::instance()->ready() &&
                        Bridge::instance()->layoutsReady() &&
                        !Bridge::instance()->activeWorkspace().isEmpty();
    if (docked) {
      ++*frames;
    }
    view_->update();
    if (*frames < 240) {
      return;
    }
    timer->stop();
    const QImage img = view_->grabWindow();
    QDir().mkpath(QFileInfo(QString::fromLocal8Bit(path)).absolutePath());
    img.save(QString::fromLocal8Bit(path));
    fflush(stdout);
    std::_Exit(0);
  });
  timer->start(16);
  /* Safety net. */
  QTimer::singleShot(120000, this, [this, path]() {
    view_->grabWindow().save(QString::fromLocal8Bit(path));
    std::_Exit(0);
  });
}

}  // namespace harness

/* -------------------------------------------------------------------- */
/** \name Hooks called by GHOST_SystemQt
 * \{ */

extern "C" void harness_shell_preinit(void)
{
  harness::bridge_register_python_module();
}

extern "C" void harness_shell_create(void)
{
  harness::Shell::instance()->create();
}

extern "C" void harness_shell_shutdown(void)
{
  harness::Shell::instance()->shutdown();
}

/** \} */
