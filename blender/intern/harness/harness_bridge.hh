/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * The data bridge between the Qt shell and Blender. Requests are queued here and taken by
 * `release/scripts/startup/harness_bridge.py` on Blender's main loop (an app timer), which
 * runs them with a proper window context and answers through the `_harness` Python module.
 * Blender also pushes its state (file, workspaces, selection, jobs) here.
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <map>

namespace harness {

class Bridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
  Q_PROPERTY(QString projectName READ projectName NOTIFY stateChanged)
  Q_PROPERTY(QString projectPath READ projectPath NOTIFY stateChanged)
  Q_PROPERTY(bool hasProject READ hasProject NOTIFY stateChanged)
  Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
  Q_PROPERTY(QStringList workspaces READ workspaces NOTIFY stateChanged)
  Q_PROPERTY(QString activeWorkspace READ activeWorkspace NOTIFY stateChanged)
  Q_PROPERTY(QString mode READ mode NOTIFY stateChanged)
  Q_PROPERTY(QString activeObject READ activeObject NOTIFY stateChanged)
  Q_PROPERTY(int selectedCount READ selectedCount NOTIFY stateChanged)
  Q_PROPERTY(int objectCount READ objectCount NOTIFY stateChanged)
  Q_PROPERTY(QString renderEngine READ renderEngine NOTIFY stateChanged)
  Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY stateChanged)
  Q_PROPERTY(QVariantList layoutTemplates READ layoutTemplates NOTIFY stateChanged)
  Q_PROPERTY(bool layoutCustomized READ layoutCustomized NOTIFY stateChanged)
  /** The picked viewport area for regional edits: [x0, y0, x1, y1] normalised, or empty. */
  Q_PROPERTY(QVariantList regionRect READ regionRect NOTIFY stateChanged)
  /** Every workspace has been laid out once (tab switches no longer rebuild panels). */
  Q_PROPERTY(bool layoutsReady READ layoutsReady NOTIFY stateChanged)
  Q_PROPERTY(QString version READ version NOTIFY stateChanged)
  Q_PROPERTY(QVariantList reports READ reports NOTIFY reportsChanged)
  Q_PROPERTY(QString jobText READ jobText NOTIFY stateChanged)
  Q_PROPERTY(double jobProgress READ jobProgress NOTIFY stateChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)

 public:
  static Bridge *instance();

  bool ready() const { return ready_; }
  QString projectName() const { return state_.value("project_name").toString(); }
  QString projectPath() const { return state_.value("project_path").toString(); }
  bool hasProject() const { return state_.value("has_project").toBool(); }
  bool modified() const { return state_.value("modified").toBool(); }
  QStringList workspaces() const { return state_.value("workspaces").toStringList(); }
  QString activeWorkspace() const { return state_.value("active_workspace").toString(); }
  QString mode() const { return state_.value("mode").toString(); }
  QString activeObject() const { return state_.value("active_object").toString(); }
  int selectedCount() const { return state_.value("selected").toInt(); }
  int objectCount() const { return state_.value("objects").toInt(); }
  QString renderEngine() const { return state_.value("render_engine").toString(); }
  QVariantList recentFiles() const { return state_.value("recent_files").toList(); }
  QVariantList layoutTemplates() const { return state_.value("layout_templates").toList(); }
  bool layoutCustomized() const { return state_.value("layout_customized").toBool(); }
  QVariantList regionRect() const { return state_.value("region_rect").toList(); }
  bool layoutsReady() const { return state_.value("layouts_ready").toBool(); }
  QString version() const { return state_.value("version").toString(); }
  QVariantList reports() const { return reports_; }
  QString jobText() const { return state_.value("job_text").toString(); }
  double jobProgress() const { return state_.value("job_progress").toDouble(); }
  bool canUndo() const { return state_.value("can_undo").toBool(); }
  bool canRedo() const { return state_.value("can_redo").toBool(); }

  /** Queue a request; `done` runs on the main thread with the reply. Returns the request id. */
  int request(const QString &kind, const QJsonObject &payload,
              std::function<void(const QJsonObject &)> done = nullptr);

  /* QML helpers. */
  Q_INVOKABLE void runOperator(const QString &op, const QVariantMap &props = {},
                               const QString &context = QStringLiteral("INVOKE_DEFAULT"));
  Q_INVOKABLE void setWorkspace(const QString &name);
  /** Panel layout of the workspaces: apply_template, save_template, delete_template, reset,
   * reset_all. */
  Q_INVOKABLE void layoutAction(const QString &action, const QString &name = QString());
  Q_INVOKABLE void openRecent(const QString &path);
  Q_INVOKABLE void callMenu(const QString &menu);
  Q_INVOKABLE void dismissReport(int index);
  /** Generic request from QML; the answer arrives through #replied with the returned id. */
  Q_INVOKABLE int query(const QString &kind, const QVariantMap &payload = {});
  Q_INVOKABLE void toggleProperty(const QString &path, const QString &prop);

  /* Called from Python (`_harness`). */
  QByteArray takeRequests();
  void reply(int id, const QByteArray &json);
  void pushState(const QByteArray &json);
  void pushReport(const QString &type, const QString &message);
  void markReady();

 Q_SIGNALS:
  void readyChanged();
  void stateChanged();
  void reportsChanged();
  void requestQueued();
  void replied(int id, const QJsonObject &result);

 private:
  struct Pending {
    QString kind;
    QJsonObject payload;
  };
  bool ready_ = false;
  QVariantMap state_;
  QVariantList reports_;
  std::vector<std::pair<int, Pending>> queue_;
  std::map<int, std::function<void(const QJsonObject &)>> callbacks_;
  int next_id_ = 1;
};

/** Register `_harness` with Python's inittab (must run before Python starts). */
void bridge_register_python_module();

}  // namespace harness
