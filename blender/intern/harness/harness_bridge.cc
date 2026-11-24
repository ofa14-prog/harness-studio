/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 */

#include <Python.h>

#include "harness_bridge.hh"
#include "GHOST_QtSurface.hh"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>

namespace harness {

Bridge *Bridge::instance()
{
  static Bridge *bridge = new Bridge();
  return bridge;
}

int Bridge::request(const QString &kind,
                    const QJsonObject &payload,
                    std::function<void(const QJsonObject &)> done)
{
  const int id = next_id_++;
  queue_.push_back({id, Pending{kind, payload}});
  if (done) {
    callbacks_[id] = std::move(done);
  }
  Q_EMIT requestQueued();
  return id;
}

void Bridge::runOperator(const QString &op, const QVariantMap &props, const QString &context)
{
  request(QStringLiteral("operator"),
          QJsonObject{{"op", op},
                      {"props", QJsonObject::fromVariantMap(props)},
                      {"context", context}});
}

void Bridge::setWorkspace(const QString &name)
{
  request(QStringLiteral("workspace"), QJsonObject{{"name", name}});
}

void Bridge::layoutAction(const QString &action, const QString &name)
{
  request(QStringLiteral("layout"), QJsonObject{{"action", action}, {"name", name}});
}

void Bridge::openRecent(const QString &path)
{
  request(QStringLiteral("open_file"), QJsonObject{{"path", path}});
}

void Bridge::callMenu(const QString &menu)
{
  request(QStringLiteral("call_menu"), QJsonObject{{"menu", menu}});
}

int Bridge::query(const QString &kind, const QVariantMap &payload)
{
  return request(kind, QJsonObject::fromVariantMap(payload));
}

void Bridge::toggleProperty(const QString &path, const QString &prop)
{
  request(QStringLiteral("toggle"), QJsonObject{{"path", path}, {"prop", prop}});
}

void Bridge::dismissReport(int index)
{
  if (index >= 0 && index < reports_.size()) {
    reports_.removeAt(index);
    Q_EMIT reportsChanged();
  }
}

QByteArray Bridge::takeRequests()
{
  QJsonArray out;
  for (auto &[id, p] : queue_) {
    out.append(QJsonObject{{"id", id}, {"kind", p.kind}, {"payload", p.payload}});
  }
  queue_.clear();
  return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

void Bridge::reply(int id, const QByteArray &json)
{
  const QJsonObject result = QJsonDocument::fromJson(json).object();
  auto it = callbacks_.find(id);
  if (it != callbacks_.end()) {
    auto cb = std::move(it->second);
    callbacks_.erase(it);
    cb(result);
  }
  Q_EMIT replied(id, result);
}

void Bridge::pushState(const QByteArray &json)
{
  const QVariantMap next = QJsonDocument::fromJson(json).object().toVariantMap();
  if (next != state_) {
    state_ = next;
    Q_EMIT stateChanged();
  }
}

void Bridge::pushReport(const QString &type, const QString &message)
{
  reports_.append(QVariantMap{{"type", type},
                              {"message", message},
                              {"time", QDateTime::currentDateTime().toString("HH:mm")}});
  while (reports_.size() > 40) {
    reports_.removeFirst();
  }
  Q_EMIT reportsChanged();
}

void Bridge::markReady()
{
  if (!ready_) {
    ready_ = true;
    Q_EMIT readyChanged();
  }
}

/* -------------------------------------------------------------------- */
/** \name `_harness` Python module
 * \{ */

static PyObject *py_take(PyObject * /*self*/, PyObject * /*args*/)
{
  const QByteArray json = Bridge::instance()->takeRequests();
  return PyUnicode_FromStringAndSize(json.constData(), json.size());
}

static PyObject *py_reply(PyObject * /*self*/, PyObject *args)
{
  int id;
  const char *json;
  if (!PyArg_ParseTuple(args, "is", &id, &json)) {
    return nullptr;
  }
  Bridge::instance()->reply(id, QByteArray(json));
  Py_RETURN_NONE;
}

static PyObject *py_state(PyObject * /*self*/, PyObject *args)
{
  const char *json;
  if (!PyArg_ParseTuple(args, "s", &json)) {
    return nullptr;
  }
  Bridge::instance()->pushState(QByteArray(json));
  Py_RETURN_NONE;
}

static PyObject *py_report(PyObject * /*self*/, PyObject *args)
{
  const char *type, *message;
  if (!PyArg_ParseTuple(args, "ss", &type, &message)) {
    return nullptr;
  }
  Bridge::instance()->pushReport(QString::fromUtf8(type), QString::fromUtf8(message));
  Py_RETURN_NONE;
}

static PyObject *py_ready(PyObject * /*self*/, PyObject * /*args*/)
{
  Bridge::instance()->markReady();
  Py_RETURN_NONE;
}

static PyObject *py_dock_next(PyObject * /*self*/, PyObject *args)
{
  const char *slot;
  if (!PyArg_ParseTuple(args, "s", &slot)) {
    return nullptr;
  }
  GHOST_QtHost::instance()->dockNext(QString::fromUtf8(slot));
  Py_RETURN_NONE;
}

static PyObject *py_has_slot(PyObject * /*self*/, PyObject *args)
{
  const char *slot;
  if (!PyArg_ParseTuple(args, "s", &slot)) {
    return nullptr;
  }
  return PyBool_FromLong(GHOST_QtHost::instance()->windowInSlot(QString::fromUtf8(slot)) != nullptr);
}

/* A font shipped in the shell's resources, written once to the cache folder so Blender's own
 * text drawing (which reads files) uses the same typeface as the shell. */
static PyObject *py_font_path(PyObject * /*self*/, PyObject *args)
{
  const char *name;
  if (!PyArg_ParseTuple(args, "s", &name)) {
    return nullptr;
  }
  const QString file = QString::fromUtf8(name);
  if (file.contains('/') || file.contains("..")) {
    Py_RETURN_NONE;
  }
  QFile res(QStringLiteral(":/harness/fonts/") + file);
  if (!res.open(QIODevice::ReadOnly)) {
    Py_RETURN_NONE;
  }
  const QByteArray data = res.readAll();
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                      QStringLiteral("/HarnessStudio/fonts");
  QDir().mkpath(dir);
  const QString out = dir + '/' + file;
  QFile disk(out);
  if (!(disk.exists() && disk.size() == data.size())) {
    QSaveFile save(out);
    if (!save.open(QIODevice::WriteOnly) || save.write(data) != data.size() || !save.commit()) {
      Py_RETURN_NONE;
    }
  }
  return PyUnicode_FromString(out.toUtf8().constData());
}

static PyMethodDef harness_methods[] = {
    {"font_path", py_font_path, METH_VARARGS, "File path of a font shipped with the shell."},
    {"take", py_take, METH_NOARGS, "Take queued shell requests (JSON list)."},
    {"reply", py_reply, METH_VARARGS, "Answer a request: reply(id, json)."},
    {"state", py_state, METH_VARARGS, "Push Blender's state to the shell (JSON object)."},
    {"report", py_report, METH_VARARGS, "Show a report in the shell: report(type, message)."},
    {"ready", py_ready, METH_NOARGS, "The bridge script is running."},
    {"dock_next", py_dock_next, METH_VARARGS, "Place the next new window in a shell slot."},
    {"has_slot", py_has_slot, METH_VARARGS, "Whether a shell slot shows a window."},
    {nullptr, nullptr, 0, nullptr},
};

static PyModuleDef harness_module = {
    PyModuleDef_HEAD_INIT, "_harness", "Harness Studio shell bridge.", -1, harness_methods,
    nullptr, nullptr, nullptr, nullptr,
};

static PyObject *PyInit__harness()
{
  return PyModule_Create(&harness_module);
}

void bridge_register_python_module()
{
  PyImport_AppendInittab("_harness", &PyInit__harness);
}

/** \} */

}  // namespace harness
