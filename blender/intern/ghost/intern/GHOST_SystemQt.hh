/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 *
 * GHOST system on Qt 6. Blender keeps its own main loop; #processEvents pumps Qt (which also
 * renders the Qt Quick shell on this thread) and hands GHOST the events the shell's surfaces
 * produced.
 */

#pragma once

#include "../GHOST_Types.hh"
#include "GHOST_Event.hh"
#include "GHOST_System.hh"

#include <QElapsedTimer>
#include <memory>

class QGuiApplication;
class GHOST_WindowQt;

class GHOST_SystemQt : public GHOST_System {
 public:
  GHOST_SystemQt();
  ~GHOST_SystemQt() override;

  bool processEvents(bool waitForEvent) override;
  bool setConsoleWindowState(GHOST_TConsoleWindowState /*action*/) override { return false; }
  GHOST_TSuccess getModifierKeys(GHOST_ModifierKeys &keys) const override;
  GHOST_TSuccess getButtons(GHOST_Buttons &buttons) const override;
  GHOST_TCapabilityFlag getCapabilities() const override;
  char *getClipboard(bool selection) const override;
  void putClipboard(const char *buffer, bool selection) const override;
  uint64_t getMilliSeconds() const override;
  uint8_t getNumDisplays() const override;
  GHOST_TSuccess getCursorPosition(int32_t &x, int32_t &y) const override;
  GHOST_TSuccess setCursorPosition(int32_t x, int32_t y) override;
  void getAllDisplayDimensions(uint32_t &width, uint32_t &height) const override;
  void getMainDisplayDimensions(uint32_t &width, uint32_t &height) const override;
  GHOST_IContext *createOffscreenContext(GHOST_GPUSettings gpu_settings) override;
  GHOST_TSuccess disposeContext(GHOST_IContext *context) override;

  /* Used by surfaces. */
  void queueEvent(std::unique_ptr<const GHOST_IEvent> event);
  void setCursorPositionShell(int32_t x, int32_t y) { cursor_x_ = x; cursor_y_ = y; }
  void setModifiersFromQt(int qt_modifiers) { qt_modifiers_ = qt_modifiers; }
  GHOST_IWindow *activeWindow() const;
  void windowClosed(GHOST_WindowQt *window);

 private:
  GHOST_TSuccess init() override;
  GHOST_IWindow *createWindow(const char *title,
                              int32_t left,
                              int32_t top,
                              uint32_t width,
                              uint32_t height,
                              GHOST_TWindowState state,
                              GHOST_GPUSettings gpu_settings,
                              const bool exclusive = false,
                              const bool is_dialog = false,
                              const GHOST_IWindow *parent_window = nullptr) override;

  QElapsedTimer clock_;
  int32_t cursor_x_ = 0, cursor_y_ = 0;
  int qt_modifiers_ = 0;
  bool shell_created_ = false;
};

/**
 * Hooks implemented by the Harness shell library (intern/harness). The GHOST back-end calls
 * them so the shell can register Python modules before Python starts and build its window.
 */
extern "C" {
void harness_shell_preinit(void);
void harness_shell_create(void);
void harness_shell_shutdown(void);
}
