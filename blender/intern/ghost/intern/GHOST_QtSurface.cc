/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

#include "GHOST_QtSurface.hh"

#include <cmath>
#include "GHOST_ContextQt.hh"
#include "GHOST_EventButton.hh"
#include "GHOST_EventCursor.hh"
#include "GHOST_EventDragnDrop.hh"
#include "GHOST_EventKey.hh"
#include "GHOST_EventTrackpad.hh"
#include "GHOST_EventWheel.hh"
#include "GHOST_SystemQt.hh"
#include "GHOST_WindowQt.hh"

#include <QCursor>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QMimeData>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QUrl>
#include <qsgtexture_platform.h>

#include <cstring>

static GHOST_SystemQt *ghost_system()
{
  return static_cast<GHOST_SystemQt *>(GHOST_ISystem::getSystem());
}

static uint64_t now_ms()
{
  return ghost_system() ? ghost_system()->getMilliSeconds() : 0;
}

/* -------------------------------------------------------------------- */
/** \name Host
 * \{ */

GHOST_QtHost *GHOST_QtHost::instance()
{
  static GHOST_QtHost *host = new GHOST_QtHost();
  return host;
}

void GHOST_QtHost::registerSurface(GHOST_QtSurface *surface)
{
  surfaces_.append(surface);
  /* A window may already be waiting for this slot. */
  for (GHOST_WindowQt *w : windows_) {
    if (!w->surface() && surface->slot().size() && !surface->ghostWindow()) {
      if (w->property_slot_ == surface->slot()) {
        surface->setGhostWindow(w);
      }
    }
  }
}

void GHOST_QtHost::unregisterSurface(GHOST_QtSurface *surface)
{
  surfaces_.removeAll(surface);
}

GHOST_WindowQt *GHOST_QtHost::windowInSlot(const QString &slot) const
{
  for (const auto &s : surfaces_) {
    if (s && s->slot() == slot) {
      return s->ghostWindow();
    }
  }
  return nullptr;
}

void GHOST_QtHost::attachWindow(GHOST_WindowQt *window)
{
  windows_.append(window);
  QString slot;
  if (!pending_dock_.isEmpty()) {
    slot = pending_dock_;
    pending_dock_.clear();
  }
  else if (!windowInSlot(QStringLiteral("center"))) {
    slot = QStringLiteral("center");
  }
  else {
    slot = QStringLiteral("float-%1").arg(++floating_counter_);
  }
  window->property_slot_ = slot;
  for (const auto &s : surfaces_) {
    if (s && s->slot() == slot && !s->ghostWindow()) {
      s->setGhostWindow(window);
      return;
    }
  }
  if (slot.startsWith(QLatin1String("float-"))) {
    Q_EMIT floatingWindowRequested(slot,
                                   QString::fromStdString(window->getTitle()),
                                   window->pixelWidth(),
                                   window->pixelHeight(),
                                   window->isDialog());
  }
}

void GHOST_QtHost::detachWindow(GHOST_WindowQt *window)
{
  windows_.removeAll(window);
  for (const auto &s : surfaces_) {
    if (s && s->ghostWindow() == window) {
      s->setGhostWindow(nullptr);
      if (s->slot().startsWith(QLatin1String("float-"))) {
        Q_EMIT floatingWindowClosed(s->slot());
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surface
 * \{ */

GHOST_QtSurface::GHOST_QtSurface(QQuickItem *parent) : QQuickItem(parent)
{
  setFlag(ItemHasContents, true);
  setFlag(ItemAcceptsInputMethod, true);
  setFlag(ItemAcceptsDrops, true);
  setFlag(ItemIsFocusScope, false);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
  setActiveFocusOnTab(false);
  GHOST_QtHost::instance()->registerSurface(this);
}

GHOST_QtSurface::~GHOST_QtSurface()
{
  GHOST_QtHost::instance()->unregisterSurface(this);
  if (window_) {
    window_->detachSurface(this);
  }
  delete texture_;
}

void GHOST_QtSurface::setSlot(const QString &slot)
{
  if (slot == slot_) {
    return;
  }
  slot_ = slot;
  Q_EMIT slotChanged();
  GHOST_QtHost::instance()->unregisterSurface(this);
  GHOST_QtHost::instance()->registerSurface(this);
}

QString GHOST_QtSurface::windowTitle() const
{
  return window_ ? QString::fromStdString(window_->getTitle()) : QString();
}

void GHOST_QtSurface::setGhostWindow(GHOST_WindowQt *window)
{
  if (window_ == window) {
    return;
  }
  if (window_) {
    window_->detachSurface(this);
  }
  window_ = window;
  shown_tex_ = 0;
  if (window_) {
    window_->attachSurface(this);
    pushGeometry();
  }
  Q_EMIT attachedChanged();
  Q_EMIT windowTitleChanged();
  update();
}

void GHOST_QtSurface::frameReady()
{
  update();
}

void GHOST_QtSurface::applyCursor(const QCursor &cursor)
{
  setCursor(cursor);
}

void GHOST_QtSurface::requestClose()
{
  if (window_ && ghost_system()) {
    ghost_system()->queueEvent(
        std::make_unique<GHOST_Event>(now_ms(), GHOST_kEventWindowClose, window_));
  }
}

void GHOST_QtSurface::pushGeometry()
{
  if (!window_ || !window()) {
    return;
  }
  const qreal dpr = window()->effectiveDevicePixelRatio();
  const QPointF origin = mapToScene(QPointF(0, 0));
  window_->setSurfaceGeometry(int(std::lround(origin.x() * dpr)),
                              int(std::lround(origin.y() * dpr)),
                              int(std::lround(width() * dpr)),
                              int(std::lround(height() * dpr)),
                              float(dpr));
}

void GHOST_QtSurface::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  pushGeometry();
}

void GHOST_QtSurface::itemChange(ItemChange change, const ItemChangeData &value)
{
  QQuickItem::itemChange(change, value);
  if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) {
    pushGeometry();
  }
}

QSGNode *GHOST_QtSurface::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
  extern int g_harness_prof_qt_frames;
  g_harness_prof_qt_frames++;
  auto *node = static_cast<QSGSimpleTextureNode *>(old);
  GHOST_ContextQt *ctx = window_ ? window_->qtContext() : nullptr;
  if (!ctx || !ctx->frontTexture() || width() <= 0 || height() <= 0) {
    delete node;
    if (texture_) {
      delete texture_;
      texture_ = nullptr;
      shown_tex_ = 0;
    }
    return nullptr;
  }
  if (!node) {
    node = new QSGSimpleTextureNode();
    node->setOwnsTexture(false);
    node->setFiltering(QSGTexture::Nearest);
    /* GL frame-buffers are bottom-up. */
    node->setTextureCoordinatesTransform(QSGSimpleTextureNode::MirrorVertically);
  }
  if (!texture_ || shown_tex_ != ctx->frontTexture() ||
      texture_->textureSize() != QSize(ctx->frontWidth(), ctx->frontHeight()))
  {
    delete texture_;
    texture_ = QNativeInterface::QSGOpenGLTexture::fromNative(
        ctx->frontTexture(),
        window(),
        QSize(ctx->frontWidth(), ctx->frontHeight()),
        QQuickWindow::TextureIsOpaque);
    shown_tex_ = ctx->frontTexture();
    node->setTexture(texture_);
  }
  /* Show the frame at its pixel size (no resampling while a resize is in flight), with its
   * corner on a whole device pixel. At fractional scales (1.25, 1.5) the item's position
   * often falls between pixels; with nearest sampling a half-pixel offset makes columns of
   * Blender's text flip between neighbouring texels, which breaks letter edges. Snapping the
   * origin keeps every texel on exactly one screen pixel. */
  const qreal dpr = window()->effectiveDevicePixelRatio();
  const QSizeF logical(ctx->frontWidth() / dpr, ctx->frontHeight() / dpr);
  const QPointF scene = mapToScene(QPointF(0, 0));
  const QPointF snap((std::round(scene.x() * dpr) - scene.x() * dpr) / dpr,
                     (std::round(scene.y() * dpr) - scene.y() * dpr) / dpr);
  node->setRect(QRectF(snap, logical));
  node->markDirty(QSGNode::DirtyMaterial);
  return node;
}

QPoint GHOST_QtSurface::toGhost(const QPointF &local) const
{
  const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  const QPointF scene = mapToScene(local);
  return QPoint(int(std::lround(scene.x() * dpr)), int(std::lround(scene.y() * dpr)));
}

void GHOST_QtSurface::sendCursor(const QPointF &pos)
{
  if (!window_) {
    return;
  }
  const QPoint p = toGhost(pos);
  ghost_system()->setCursorPositionShell(p.x(), p.y());
  ghost_system()->queueEvent(std::make_unique<GHOST_EventCursor>(
      now_ms(), GHOST_kEventCursorMove, window_, p.x(), p.y(), GHOST_TABLET_DATA_NONE));
}

static GHOST_TButton ghost_button(Qt::MouseButton b)
{
  switch (b) {
    case Qt::LeftButton: return GHOST_kButtonMaskLeft;
    case Qt::MiddleButton: return GHOST_kButtonMaskMiddle;
    case Qt::RightButton: return GHOST_kButtonMaskRight;
    case Qt::BackButton: return GHOST_kButtonMaskButton4;
    case Qt::ForwardButton: return GHOST_kButtonMaskButton5;
    default: return GHOST_kButtonMaskNone;
  }
}

void GHOST_QtSurface::mousePressEvent(QMouseEvent *event)
{
  if (!window_) {
    event->ignore();
    return;
  }
  if (!hasActiveFocus()) {
    forceActiveFocus(Qt::MouseFocusReason);
  }
  sendCursor(event->position());
  const GHOST_TButton b = ghost_button(event->button());
  if (b != GHOST_kButtonMaskNone) {
    ghost_system()->queueEvent(std::make_unique<GHOST_EventButton>(
        now_ms(), GHOST_kEventButtonDown, window_, b, GHOST_TABLET_DATA_NONE));
  }
  event->accept();
}

void GHOST_QtSurface::mouseDoubleClickEvent(QMouseEvent *event)
{
  /* Blender detects double clicks itself from press timing. */
  mousePressEvent(event);
}

void GHOST_QtSurface::mouseReleaseEvent(QMouseEvent *event)
{
  if (!window_) {
    return;
  }
  sendCursor(event->position());
  const GHOST_TButton b = ghost_button(event->button());
  if (b != GHOST_kButtonMaskNone) {
    ghost_system()->queueEvent(std::make_unique<GHOST_EventButton>(
        now_ms(), GHOST_kEventButtonUp, window_, b, GHOST_TABLET_DATA_NONE));
  }
  event->accept();
}

void GHOST_QtSurface::mouseMoveEvent(QMouseEvent *event)
{
  sendCursor(event->position());
  event->accept();
}

void GHOST_QtSurface::hoverMoveEvent(QHoverEvent *event)
{
  sendCursor(event->position());
}

static bool text_input_has_focus(QQuickWindow *w)
{
  QQuickItem *item = w ? w->activeFocusItem() : nullptr;
  if (!item) {
    return false;
  }
  const char *cls = item->metaObject()->className();
  return strstr(cls, "TextInput") || strstr(cls, "TextEdit") || strstr(cls, "TextField") ||
         strstr(cls, "TextArea");
}

void GHOST_QtSurface::hoverEnterEvent(QHoverEvent *event)
{
  /* Blender routes keys to the region under the pointer: take keyboard focus when the
   * pointer enters, unless the person is typing into a text field elsewhere. */
  if (window_ && !hasActiveFocus() && !text_input_has_focus(window())) {
    forceActiveFocus(Qt::OtherFocusReason);
  }
  sendCursor(event->position());
}

void GHOST_QtSurface::hoverLeaveEvent(QHoverEvent * /*event*/) {}

void GHOST_QtSurface::wheelEvent(QWheelEvent *event)
{
  if (!window_) {
    event->ignore();
    return;
  }
  sendCursor(event->position());
  const QPoint pixel = event->pixelDelta();
  const QPoint angle = event->angleDelta();
  const bool is_touchpad = !pixel.isNull() && (angle.y() % 120 != 0 || angle.x() % 120 != 0);
  if (is_touchpad) {
    const QPoint p = toGhost(event->position());
    const qreal dpr = window()->effectiveDevicePixelRatio();
    ghost_system()->queueEvent(std::make_unique<GHOST_EventTrackpad>(
        now_ms(), window_, GHOST_kTrackpadEventScroll, p.x(), p.y(),
        int(std::lround(-pixel.x() * dpr)), int(std::lround(-pixel.y() * dpr)),
        event->inverted()));
  }
  else {
    wheel_accum_y_ += angle.y() / 120.0;
    wheel_accum_x_ += angle.x() / 120.0;
    const int steps_y = int(wheel_accum_y_);
    const int steps_x = int(wheel_accum_x_);
    wheel_accum_y_ -= steps_y;
    wheel_accum_x_ -= steps_x;
    if (steps_y) {
      ghost_system()->queueEvent(std::make_unique<GHOST_EventWheel>(
          now_ms(), window_, GHOST_kEventWheelAxisVertical, steps_y));
    }
    if (steps_x) {
      ghost_system()->queueEvent(std::make_unique<GHOST_EventWheel>(
          now_ms(), window_, GHOST_kEventWheelAxisHorizontal, -steps_x));
    }
  }
  event->accept();
}

static GHOST_TKey ghost_key(const QKeyEvent *e)
{
  const int k = e->key();
  const bool keypad = e->modifiers() & Qt::KeypadModifier;
  if (keypad) {
    if (k >= Qt::Key_0 && k <= Qt::Key_9) {
      return GHOST_TKey(GHOST_kKeyNumpad0 + (k - Qt::Key_0));
    }
    switch (k) {
      case Qt::Key_Period: case Qt::Key_Comma: case Qt::Key_Delete: return GHOST_kKeyNumpadPeriod;
      case Qt::Key_Enter: return GHOST_kKeyNumpadEnter;
      case Qt::Key_Plus: return GHOST_kKeyNumpadPlus;
      case Qt::Key_Minus: return GHOST_kKeyNumpadMinus;
      case Qt::Key_Asterisk: return GHOST_kKeyNumpadAsterisk;
      case Qt::Key_Slash: return GHOST_kKeyNumpadSlash;
      case Qt::Key_Insert: return GHOST_kKeyNumpad0;
      case Qt::Key_End: return GHOST_kKeyNumpad1;
      case Qt::Key_Down: return GHOST_kKeyNumpad2;
      case Qt::Key_PageDown: return GHOST_kKeyNumpad3;
      case Qt::Key_Left: return GHOST_kKeyNumpad4;
      case Qt::Key_Clear: return GHOST_kKeyNumpad5;
      case Qt::Key_Right: return GHOST_kKeyNumpad6;
      case Qt::Key_Home: return GHOST_kKeyNumpad7;
      case Qt::Key_Up: return GHOST_kKeyNumpad8;
      case Qt::Key_PageUp: return GHOST_kKeyNumpad9;
      default: break;
    }
  }
  if (k >= Qt::Key_A && k <= Qt::Key_Z) {
    return GHOST_TKey(GHOST_kKeyA + (k - Qt::Key_A));
  }
  if (k >= Qt::Key_0 && k <= Qt::Key_9) {
    return GHOST_TKey(GHOST_kKey0 + (k - Qt::Key_0));
  }
  if (k >= Qt::Key_F1 && k <= Qt::Key_F24) {
    return GHOST_TKey(GHOST_kKeyF1 + (k - Qt::Key_F1));
  }
  /* Layout-independent keys by native scan code would be better; Qt keys cover the rest. */
  switch (k) {
    case Qt::Key_Backspace: return GHOST_kKeyBackSpace;
    case Qt::Key_Tab: case Qt::Key_Backtab: return GHOST_kKeyTab;
    case Qt::Key_Return: return GHOST_kKeyEnter;
    case Qt::Key_Enter: return GHOST_kKeyNumpadEnter;
    case Qt::Key_Escape: return GHOST_kKeyEsc;
    case Qt::Key_Space: return GHOST_kKeySpace;
    case Qt::Key_Apostrophe: return GHOST_kKeyQuote;
    case Qt::Key_Comma: return GHOST_kKeyComma;
    case Qt::Key_Minus: return GHOST_kKeyMinus;
    case Qt::Key_Plus: return GHOST_kKeyPlus;
    case Qt::Key_Period: return GHOST_kKeyPeriod;
    case Qt::Key_Slash: return GHOST_kKeySlash;
    case Qt::Key_Semicolon: return GHOST_kKeySemicolon;
    case Qt::Key_Equal: return GHOST_kKeyEqual;
    case Qt::Key_BracketLeft: return GHOST_kKeyLeftBracket;
    case Qt::Key_BracketRight: return GHOST_kKeyRightBracket;
    case Qt::Key_Backslash: return GHOST_kKeyBackslash;
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return GHOST_kKeyAccentGrave;
    case Qt::Key_Shift: return GHOST_kKeyLeftShift;
    case Qt::Key_Control: return GHOST_kKeyLeftControl;
    case Qt::Key_Alt: return GHOST_kKeyLeftAlt;
    case Qt::Key_AltGr: return GHOST_kKeyRightAlt;
    case Qt::Key_Meta: case Qt::Key_Super_L: return GHOST_kKeyLeftOS;
    case Qt::Key_Super_R: return GHOST_kKeyRightOS;
    case Qt::Key_Menu: return GHOST_kKeyApp;
    case Qt::Key_CapsLock: return GHOST_kKeyCapsLock;
    case Qt::Key_NumLock: return GHOST_kKeyNumLock;
    case Qt::Key_ScrollLock: return GHOST_kKeyScrollLock;
    case Qt::Key_Left: return GHOST_kKeyLeftArrow;
    case Qt::Key_Right: return GHOST_kKeyRightArrow;
    case Qt::Key_Up: return GHOST_kKeyUpArrow;
    case Qt::Key_Down: return GHOST_kKeyDownArrow;
    case Qt::Key_Print: return GHOST_kKeyPrintScreen;
    case Qt::Key_Pause: return GHOST_kKeyPause;
    case Qt::Key_Insert: return GHOST_kKeyInsert;
    case Qt::Key_Delete: return GHOST_kKeyDelete;
    case Qt::Key_Home: return GHOST_kKeyHome;
    case Qt::Key_End: return GHOST_kKeyEnd;
    case Qt::Key_PageUp: return GHOST_kKeyUpPage;
    case Qt::Key_PageDown: return GHOST_kKeyDownPage;
    case Qt::Key_Less: case Qt::Key_Greater: case Qt::Key_Bar: return GHOST_kKeyGrLess;
    case Qt::Key_MediaPlay: return GHOST_kKeyMediaPlay;
    case Qt::Key_MediaStop: return GHOST_kKeyMediaStop;
    case Qt::Key_MediaPrevious: return GHOST_kKeyMediaFirst;
    case Qt::Key_MediaNext: return GHOST_kKeyMediaLast;
    default: break;
  }
  /* Non-Latin layouts: fall back to the scan code row (X11/Wayland evdev codes + 8). */
  const quint32 sc = e->nativeScanCode();
  static const GHOST_TKey row_q[] = {GHOST_kKeyQ, GHOST_kKeyW, GHOST_kKeyE, GHOST_kKeyR,
                                     GHOST_kKeyT, GHOST_kKeyY, GHOST_kKeyU, GHOST_kKeyI,
                                     GHOST_kKeyO, GHOST_kKeyP};
  static const GHOST_TKey row_a[] = {GHOST_kKeyA, GHOST_kKeyS, GHOST_kKeyD, GHOST_kKeyF,
                                     GHOST_kKeyG, GHOST_kKeyH, GHOST_kKeyJ, GHOST_kKeyK,
                                     GHOST_kKeyL};
  static const GHOST_TKey row_z[] = {GHOST_kKeyZ, GHOST_kKeyX, GHOST_kKeyC, GHOST_kKeyV,
                                     GHOST_kKeyB, GHOST_kKeyN, GHOST_kKeyM};
  if (sc >= 24 && sc <= 33) return row_q[sc - 24];
  if (sc >= 38 && sc <= 46) return row_a[sc - 38];
  if (sc >= 52 && sc <= 58) return row_z[sc - 52];
  return GHOST_kKeyUnknown;
}

void GHOST_QtSurface::keyPressEvent(QKeyEvent *event)
{
  if (!window_) {
    event->ignore();
    return;
  }
  const GHOST_TKey key = ghost_key(event);
  char utf8[sizeof(GHOST_TEventKeyData::utf8_buf)] = {'\0'};
  const QByteArray text = event->text().toUtf8();
  if (!text.isEmpty() && uint8_t(text[0]) >= 32 && text[0] != 127) {
    const size_t n = std::min(size_t(text.size()), sizeof(utf8) - 1);
    memcpy(utf8, text.constData(), n);
    utf8[n] = '\0';
  }
  ghost_system()->queueEvent(std::make_unique<GHOST_EventKey>(
      now_ms(), GHOST_kEventKeyDown, window_, key, event->isAutoRepeat(), utf8));
  event->accept();
}

void GHOST_QtSurface::keyReleaseEvent(QKeyEvent *event)
{
  if (!window_ || event->isAutoRepeat()) {
    event->accept();
    return;
  }
  ghost_system()->queueEvent(std::make_unique<GHOST_EventKey>(
      now_ms(), GHOST_kEventKeyUp, window_, ghost_key(event), false));
  event->accept();
}

void GHOST_QtSurface::focusInEvent(QFocusEvent *event)
{
  QQuickItem::focusInEvent(event);
  if (window_) {
    ghost_system()->queueEvent(
        std::make_unique<GHOST_Event>(now_ms(), GHOST_kEventWindowActivate, window_));
  }
}

void GHOST_QtSurface::focusOutEvent(QFocusEvent *event)
{
  QQuickItem::focusOutEvent(event);
}

void GHOST_QtSurface::inputMethodEvent(QInputMethodEvent *event)
{
  /* Composed text (dead keys, IME) arrives as a string; send it as key presses with text. */
  if (!window_) {
    return;
  }
  const QString commit = event->commitString();
  for (const QChar c : commit) {
    const QByteArray utf8_text = QString(c).toUtf8();
    char utf8[sizeof(GHOST_TEventKeyData::utf8_buf)] = {'\0'};
    memcpy(utf8, utf8_text.constData(), std::min(size_t(utf8_text.size()), sizeof(utf8) - 1));
    ghost_system()->queueEvent(std::make_unique<GHOST_EventKey>(
        now_ms(), GHOST_kEventKeyDown, window_, GHOST_kKeyUnknown, false, utf8));
  }
  event->accept();
}

QVariant GHOST_QtSurface::inputMethodQuery(Qt::InputMethodQuery query) const
{
  if (query == Qt::ImEnabled) {
    return true;
  }
  return QQuickItem::inputMethodQuery(query);
}

void GHOST_QtSurface::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void GHOST_QtSurface::dragMoveEvent(QDragMoveEvent *event)
{
  sendCursor(event->position());
  event->acceptProposedAction();
}

void GHOST_QtSurface::dropEvent(QDropEvent *event)
{
  if (!window_ || !event->mimeData()->hasUrls()) {
    return;
  }
  QStringList paths;
  for (const QUrl &u : event->mimeData()->urls()) {
    if (u.isLocalFile()) {
      paths << u.toLocalFile();
    }
  }
  if (paths.isEmpty()) {
    return;
  }
  auto *array = static_cast<GHOST_TStringArray *>(malloc(sizeof(GHOST_TStringArray)));
  array->count = int(paths.size());
  array->strings = static_cast<uint8_t **>(malloc(sizeof(uint8_t *) * size_t(paths.size())));
  for (int i = 0; i < paths.size(); i++) {
    const QByteArray p = paths[i].toUtf8();
    array->strings[i] = reinterpret_cast<uint8_t *>(strdup(p.constData()));
  }
  const QPoint p = toGhost(event->position());
  ghost_system()->queueEvent(std::make_unique<GHOST_EventDragnDrop>(
      now_ms(), GHOST_kEventDraggingDropDone, GHOST_kDragnDropTypeFilenames, window_, p.x(),
      p.y(), array));
  event->acceptProposedAction();
}

/** \} */
