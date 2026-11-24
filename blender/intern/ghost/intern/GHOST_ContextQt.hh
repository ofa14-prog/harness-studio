/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * OpenGL context for the Qt 6 GHOST back-end.
 *
 * Every context is a QOpenGLContext in Qt's global share group, so textures are visible to the
 * Qt Quick scene graph. Window contexts render into an off-screen frame-buffer that keeps the
 * same GL name for its whole life (the GPU module caches it), and #swapBufferRelease copies the
 * finished frame into a "front" texture the window's Qt Quick item displays.
 */

#pragma once

#include "GHOST_Context.hh"

class QOpenGLContext;
class QOffscreenSurface;
class GHOST_WindowQt;

class GHOST_ContextQt : public GHOST_Context {
 public:
  GHOST_ContextQt(const GHOST_ContextParams &context_params, GHOST_WindowQt *window);
  ~GHOST_ContextQt() override;

  GHOST_TSuccess swapBufferAcquire() override;
  GHOST_TSuccess swapBufferRelease() override;
  GHOST_TSuccess activateDrawingContext() override;
  GHOST_TSuccess releaseDrawingContext() override;
  GHOST_TSuccess initializeDrawingContext() override;
  GHOST_TSuccess releaseNativeHandles() override;
  GHOST_TSuccess setSwapInterval(int interval) override;
  GHOST_TSuccess getSwapInterval(int &interval_out) override;
  unsigned int getDefaultFramebuffer() override;

  /** Make this context current again after Qt Quick rendered with its own. */
  void restoreCurrent();
  /** The texture holding the last finished frame (0 before the first frame). */
  unsigned int frontTexture() const { return front_tex_; }
  int frontWidth() const { return front_w_; }
  int frontHeight() const { return front_h_; }

 private:
  void ensureTargets();
  void paceFrame();
  void destroyTargets();

  GHOST_WindowQt *window_;
  QOpenGLContext *context_ = nullptr;
  QOffscreenSurface *surface_ = nullptr;
  /* Back buffer Blender draws into (stable FBO name). */
  unsigned int fbo_ = 0, color_tex_ = 0, depth_rb_ = 0;
  /* Front buffer shown by Qt. */
  unsigned int front_fbo_ = 0, front_tex_ = 0;
  int width_ = 0, height_ = 0;
  int front_w_ = 0, front_h_ = 0;
  int swap_interval_ = 0;
};
