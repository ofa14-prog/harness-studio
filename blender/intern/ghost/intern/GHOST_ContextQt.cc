/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

#include "GHOST_ContextQt.hh"
#include "GHOST_QtSurface.hh"
#include "GHOST_WindowQt.hh"

#include <epoxy/gl.h>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QThread>
#include <QScreen>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

GHOST_ContextQt::GHOST_ContextQt(const GHOST_ContextParams &context_params,
                                 GHOST_WindowQt *window)
    : GHOST_Context(context_params), window_(window)
{
}

GHOST_ContextQt::~GHOST_ContextQt()
{
  if (context_) {
    if (context_->makeCurrent(surface_)) {
      destroyTargets();
    }
    if (active_context_ == this) {
      active_context_ = nullptr;
    }
    context_->doneCurrent();
  }
  delete context_;
  delete surface_;
}

GHOST_TSuccess GHOST_ContextQt::initializeDrawingContext()
{
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setVersion(4, 3);
  if (context_params_.is_debug) {
    format.setOption(QSurfaceFormat::DebugContext);
  }

  surface_ = new QOffscreenSurface();
  surface_->setFormat(format);
  surface_->create();

  context_ = new QOpenGLContext();
  context_->setFormat(format);
  /* Same share group as Qt Quick, so window textures can be shown by the scene graph. */
  context_->setShareContext(QOpenGLContext::globalShareContext());
  if (!context_->create()) {
    return GHOST_kFailure;
  }
  const QSurfaceFormat actual = context_->format();
  if (actual.majorVersion() < 4 || (actual.majorVersion() == 4 && actual.minorVersion() < 3)) {
    return GHOST_kFailure;
  }
  if (!context_->makeCurrent(surface_)) {
    return GHOST_kFailure;
  }
  active_context_ = this;
  if (window_) {
    ensureTargets();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    initClearGL();
  }
  return GHOST_kSuccess;
}

void GHOST_ContextQt::destroyTargets()
{
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  if (color_tex_) glDeleteTextures(1, &color_tex_);
  if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
  if (front_fbo_) glDeleteFramebuffers(1, &front_fbo_);
  if (front_tex_) glDeleteTextures(1, &front_tex_);
  fbo_ = color_tex_ = depth_rb_ = front_fbo_ = front_tex_ = 0;
  width_ = height_ = 0;
}

void GHOST_ContextQt::ensureTargets()
{
  if (!window_) {
    return;
  }
  const int w = std::max(1, window_->pixelWidth());
  const int h = std::max(1, window_->pixelHeight());
  if (fbo_ && w == width_ && h == height_) {
    return;
  }
  GLint prev_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

  if (!fbo_) {
    glGenFramebuffers(1, &fbo_);
    glGenFramebuffers(1, &front_fbo_);
  }
  /* Re-create the attachments; the frame-buffer names stay the same. The old picture is
   * carried over and the rest cleared, so regions Blender doesn't redraw this frame never show
   * undefined memory; then Blender is asked to redraw everything at the new size. */
  const GLuint old_color = color_tex_;
  const int old_w = width_, old_h = height_;
  if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);

  glGenTextures(1, &color_tex_);
  glBindTexture(GL_TEXTURE_2D, color_tex_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glGenRenderbuffers(1, &depth_rb_);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.114f, 0.114f, 0.114f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  if (old_color) {
    if (front_tex_ && front_w_ == old_w && front_h_ == old_h) {
      /* The last finished frame lives in the front texture. Blender's window origin is the
       * bottom-left corner, so keep the top edge aligned. */
      const int cw = std::min(old_w, w), ch = std::min(old_h, h);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, front_fbo_);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
      glBlitFramebuffer(0, old_h - ch, cw, old_h, 0, h - ch, cw, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glDeleteTextures(1, &old_color);
  }
  if (old_w != 0) {
    window_->requestFullRedraw();
  }

  /* The front texture is re-created at the new size on the next swap. */
  width_ = w;
  height_ = h;
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo ? GLuint(prev_fbo) : fbo_);
}

GHOST_TSuccess GHOST_ContextQt::swapBufferAcquire()
{
  if (window_) {
    ensureTargets();
    paceFrame();
  }
  return GHOST_kSuccess;
}

void GHOST_ContextQt::paceFrame()
{
  /* Frames faster than the display refreshes are never seen; they only heat the GPU (and a hot
   * laptop GPU slows down). Keep Blender's window frames at most one per refresh. The wait is
   * short (under one refresh) and never happens while the loop is idle. */
  static const bool uncapped = qEnvironmentVariableIsSet("HARNESS_UNCAPPED");
  if (uncapped) {
    return;
  }
  static QElapsedTimer clock;
  static qint64 last_ns = 0;
  if (!clock.isValid()) {
    clock.start();
  }
  qreal hz = 60.0;
  if (QScreen *screen = QGuiApplication::primaryScreen()) {
    if (screen->refreshRate() >= 30.0) {
      hz = screen->refreshRate();
    }
  }
  const qint64 interval = qint64(1e9 / hz * 0.97);
  const qint64 now = clock.nsecsElapsed();
  const qint64 wait = last_ns + interval - now;
  if (wait > 0 && wait < interval) {
    QThread::usleep(quint64(wait / 1000));
  }
  last_ns = clock.nsecsElapsed();
}

extern int g_harness_prof_blender_frames;
extern double g_harness_prof_swap_ms;

GHOST_TSuccess GHOST_ContextQt::swapBufferRelease()
{
  g_harness_prof_blender_frames++;
  QElapsedTimer prof;
  prof.start();
  struct ProfEnd {
    QElapsedTimer &t;
    ~ProfEnd() { g_harness_prof_swap_ms += t.nsecsElapsed() / 1e6; }
  } prof_end{prof};
  if (!window_ || !fbo_) {
    return GHOST_kSuccess;
  }
  /* Copy the finished frame into the front texture (new texture on size change, so the
   * scene graph never samples a texture that is being re-specified). */
  if (!front_tex_ || front_w_ != width_ || front_h_ != height_) {
    if (front_tex_) {
      glDeleteTextures(1, &front_tex_);
    }
    glGenTextures(1, &front_tex_);
    glBindTexture(GL_TEXTURE_2D, front_tex_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width_, height_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, front_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, front_tex_, 0);
    front_w_ = width_;
    front_h_ = height_;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, front_fbo_);
  glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  /* Blender leaves translucent alpha in places (the status bar header is drawn at 70%); a
   * window shows its frame-buffer opaque, so make the shown copy opaque too, otherwise the
   * desktop (or white, in screenshots) shows through. */
  {
    GLboolean mask[4];
    GLfloat clear[4];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, front_fbo_);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (scissor) {
      glEnable(GL_SCISSOR_TEST);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFlush();
  window_->frameReady();
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextQt::activateDrawingContext()
{
  if (!context_) {
    return GHOST_kFailure;
  }
  if (!context_->makeCurrent(surface_)) {
    return GHOST_kFailure;
  }
  active_context_ = this;
  return GHOST_kSuccess;
}

void GHOST_ContextQt::restoreCurrent()
{
  if (context_) {
    context_->makeCurrent(surface_);
  }
}

GHOST_TSuccess GHOST_ContextQt::releaseDrawingContext()
{
  if (!context_) {
    return GHOST_kFailure;
  }
  context_->doneCurrent();
  if (active_context_ == this) {
    active_context_ = nullptr;
  }
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextQt::releaseNativeHandles()
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextQt::setSwapInterval(int interval)
{
  /* Presentation is paced by Qt Quick; remember the request only. */
  swap_interval_ = interval;
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextQt::getSwapInterval(int &interval_out)
{
  interval_out = swap_interval_;
  return GHOST_kSuccess;
}

unsigned int GHOST_ContextQt::getDefaultFramebuffer()
{
  return window_ ? fbo_ : 0;
}
