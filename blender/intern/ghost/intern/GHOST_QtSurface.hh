/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * Qt Quick item that shows one Blender (GHOST) window and turns Qt input into GHOST events.
 * The shell places these in its layout by `slot` ("center", "left", or a floating id).
 */

#pragma once

#include <QQuickItem>
#include <QPointer>
#include <QString>

class GHOST_WindowQt;
class QSGTexture;

class GHOST_QtSurface : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QString slot READ slot WRITE setSlot NOTIFY slotChanged)
  Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)
  Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)

 public:
  explicit GHOST_QtSurface(QQuickItem *parent = nullptr);
  ~GHOST_QtSurface() override;

  QString slot() const { return slot_; }
  void setSlot(const QString &slot);
  bool attached() const { return window_ != nullptr; }
  QString windowTitle() const;

  void setGhostWindow(GHOST_WindowQt *window);
  GHOST_WindowQt *ghostWindow() const { return window_; }
  void frameReady();
  void titleChanged() { Q_EMIT windowTitleChanged(); }
  void applyCursor(const QCursor &cursor);

  /** Ask Blender to close this window (as the window manager's close button would). */
  Q_INVOKABLE void requestClose();

 Q_SIGNALS:
  void slotChanged();
  void attachedChanged();
  void windowTitleChanged();

 protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
  void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
  void itemChange(ItemChange change, const ItemChangeData &value) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void hoverMoveEvent(QHoverEvent *event) override;
  void hoverEnterEvent(QHoverEvent *event) override;
  void hoverLeaveEvent(QHoverEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  void inputMethodEvent(QInputMethodEvent *event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

 private:
  void pushGeometry();
  void sendCursor(const QPointF &pos);
  QPoint toGhost(const QPointF &local) const;

  QString slot_;
  GHOST_WindowQt *window_ = nullptr;
  unsigned int shown_tex_ = 0;
  QSGTexture *texture_ = nullptr;
  double wheel_accum_x_ = 0.0, wheel_accum_y_ = 0.0;
};

/**
 * Registry between GHOST windows and the surfaces the shell creates.
 * The first window goes to "center"; a window opened after #dockNext("left") goes to "left";
 * every other window gets its own floating top-level surface (preferences, render view...).
 */
class GHOST_QtHost : public QObject {
  Q_OBJECT
 public:
  static GHOST_QtHost *instance();

  void registerSurface(GHOST_QtSurface *surface);
  void unregisterSurface(GHOST_QtSurface *surface);
  void attachWindow(GHOST_WindowQt *window);
  void detachWindow(GHOST_WindowQt *window);
  void dockNext(const QString &slot) { pending_dock_ = slot; }
  GHOST_WindowQt *windowInSlot(const QString &slot) const;

 Q_SIGNALS:
  /** No surface is waiting for this window: the shell should create a floating one. */
  void floatingWindowRequested(const QString &slot, const QString &title, int width, int height,
                               bool dialog);
  void floatingWindowClosed(const QString &slot);
  void mainTitleChanged(const QString &title, bool modified);

 private:
  QList<QPointer<GHOST_QtSurface>> surfaces_;
  QList<GHOST_WindowQt *> windows_;
  QString pending_dock_;
  int floating_counter_ = 0;
  friend class GHOST_WindowQt;
};
