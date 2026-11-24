/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * A GHOST window of the Qt 6 back-end. It is not a native window: Blender draws it into a
 * texture, and a #GHOST_QtSurface item placed anywhere in the Qt Quick shell shows it and
 * feeds its input back as GHOST events.
 */

#pragma once

#include "GHOST_Window.hh"

#include <QPointer>
#include <QString>
#include <string>

class GHOST_SystemQt;
class GHOST_QtSurface;
class GHOST_ContextQt;

class GHOST_WindowQt : public GHOST_Window {
 public:
  GHOST_WindowQt(GHOST_SystemQt *system,
                 const char *title,
                 int32_t left,
                 int32_t top,
                 uint32_t width,
                 uint32_t height,
                 GHOST_TWindowState state,
                 GHOST_TDrawingContextType type,
                 const GHOST_ContextParams &context_params,
                 bool is_dialog,
                 const GHOST_IWindow *parent_window);
  ~GHOST_WindowQt() override;

  bool getValid() const override;
  void setTitle(const char *title) override;
  std::string getTitle() const override;
  void getWindowBounds(GHOST_Rect &bounds) const override;
  void getClientBounds(GHOST_Rect &bounds) const override;
  GHOST_TSuccess setClientWidth(uint32_t width) override;
  GHOST_TSuccess setClientHeight(uint32_t height) override;
  GHOST_TSuccess setClientSize(uint32_t width, uint32_t height) override;
  void screenToClient(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const override;
  void clientToScreen(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const override;
  GHOST_TWindowState getState() const override;
  GHOST_TSuccess setState(GHOST_TWindowState state) override;
  GHOST_TSuccess setOrder(GHOST_TWindowOrder order) override;
  GHOST_TSuccess invalidate() override;
  uint16_t getDPIHint() override;
  bool isDialog() const override { return is_dialog_; }
  GHOST_TSuccess setModifiedState(bool is_unsaved_changes) override;

  /* Shell side. */
  void attachSurface(GHOST_QtSurface *surface);
  void detachSurface(GHOST_QtSurface *surface);
  GHOST_QtSurface *surface() const { return surface_; }
  /** Called by the surface when it moves or resizes (device pixels, shell coordinates). */
  void setSurfaceGeometry(int x, int y, int w, int h, float dpr);
  int pixelWidth() const { return width_; }
  int pixelHeight() const { return height_; }
  float devicePixelRatio() const { return dpr_; }
  GHOST_ContextQt *qtContext() const;
  /** The context finished a frame: ask the surface to show it. */
  void frameReady();
  /** Ask Blender to redraw every region (after the back buffer lost its contents). */
  void requestFullRedraw();
  bool hasFrame() const { return has_frame_; }
  bool modified() const { return modified_; }
  int64_t id() const { return id_; }
  const GHOST_IWindow *parentWindow() const { return parent_; }
  /** The shell slot this window was assigned to. */
  QString property_slot_;

 protected:
  GHOST_Context *newDrawingContext(GHOST_TDrawingContextType type) override;
  GHOST_TSuccess setWindowCursorGrab(GHOST_TGrabCursorMode mode) override;
  GHOST_TSuccess setWindowCursorShape(GHOST_TStandardCursor shape) override;
  GHOST_TSuccess hasCursorShape(GHOST_TStandardCursor shape) override;
  GHOST_TSuccess setWindowCustomCursorShape(const uint8_t *bitmap,
                                            const uint8_t *mask,
                                            const int size[2],
                                            const int hot_spot[2],
                                            bool can_invert_color) override;
  GHOST_TSuccess setWindowCursorVisibility(bool visible) override;

 private:
  void applyCursor();

  GHOST_SystemQt *system_;
  QPointer<GHOST_QtSurface> surface_;
  std::string title_;
  int x_ = 0, y_ = 0;
  int width_, height_;
  float dpr_ = 1.0f;
  GHOST_TWindowState state_;
  bool is_dialog_;
  bool modified_ = false;
  bool has_frame_ = false;
  bool cursor_visible_ = true;
  int cursor_shape_qt_ = 0;     /* Qt::CursorShape */
  bool cursor_custom_ = false;
  void *custom_cursor_ = nullptr; /* QCursor* */
  int64_t id_;
  const GHOST_IWindow *parent_;
};
