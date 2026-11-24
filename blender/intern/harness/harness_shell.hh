/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * The Harness Studio shell: one frameless Qt Quick window holding the title bar, the workspace
 * carousel, Blender's windows (as GHOST_QtSurface items) and the Harness Studio side panel.
 */

#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <map>

class QQuickView;
class QQmlEngine;

namespace harness {

class Shell : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool maximized READ maximized NOTIFY windowStateChanged)
  Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY windowStateChanged)

 public:
  static Shell *instance();
  void create();
  void shutdown();

  bool maximized() const;
  qreal devicePixelRatio() const;

  Q_INVOKABLE void minimize();
  Q_INVOKABLE void toggleMaximize();
  Q_INVOKABLE void close();
  Q_INVOKABLE void startMove();
  Q_INVOKABLE void startResize(int edges);
  Q_INVOKABLE QString fileUrl(const QString &path) const;
  /** An image on the clipboard (copied pixels or a copied image file), saved as a file;
   * its URL, or empty when the clipboard holds no image. */
  Q_INVOKABLE QString clipboardImage() const;
  /** Save the window as HARNESS_SCREENSHOT and quit (agent tests). */
  Q_INVOKABLE void captureAndExit();
  /** Whether a URL or path names an image Qt can show. */
  Q_INVOKABLE bool isImage(const QString &url) const;
  /** An environment variable (for checks such as HARNESS_VIEW=<file> opening the viewer). */
  Q_INVOKABLE QString env(const QString &name) const { return qEnvironmentVariable(name.toUtf8().constData()); }
  Q_INVOKABLE void openExternally(const QString &url) const;
  Q_INVOKABLE void copyText(const QString &text) const;

 Q_SIGNALS:
  void windowStateChanged();

 private:
  void createFloating(const QString &slot, const QString &title, int w, int h, bool dialog);
  void setupEngine(QQmlEngine *engine);
  void scheduleScreenshot();

  QQuickView *view_ = nullptr;
  std::map<QString, QPointer<QQuickView>> floating_;
};

}  // namespace harness
