/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * The agent: a tool-using loop over the configured chat provider, working on the live
 * Blender scene through the bridge.
 */

#include "../harness_bridge.hh"
#include "profiles.hh"
#include "studio.hh"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QUuid>

namespace harness {

void Studio::attach(Profiles *profiles)
{
  profiles_ = profiles;
  net_ = new QNetworkAccessManager(this);
  loadModelCache();
  loadCatalog();
  loadJobs();
  save_timer_.setSingleShot(true);
  save_timer_.setInterval(400);
  connect(&save_timer_, &QTimer::timeout, this, [this]() { saveJobs(); });
  connect(this, &Studio::jobsChanged, this, [this]() { save_timer_.start(); });
  poll_timer_.setInterval(1500);
  connect(&poll_timer_, &QTimer::timeout, this, &Studio::pollJobs);
  connect(profiles_, &Profiles::currentChanged, this, [this]() {
    loadThreads();
    newThread();
    Q_EMIT providersChanged();
    Q_EMIT agentModelChanged();
    Q_EMIT createChanged();
  });
  connect(profiles_, &Profiles::keysChanged, this, [this]() {
    Q_EMIT providersChanged();
    Q_EMIT modelsChanged();
  });
  connect(profiles_, &Profiles::settingsChanged, this, [this]() {
    Q_EMIT agentModelChanged();
    Q_EMIT modelsChanged();
  });
  create_kind_ = profiles_->get("create.kind").toString();
  create_source_ = profiles_->get("create.source").toString();
  loadThreads();
  newThread();
  connect(Bridge::instance(), &Bridge::stateChanged, this,
          [this]() { projectChanged(Bridge::instance()->projectPath()); });
  /* Model lists are fetched live once per session; the cache covers the start. */
  QTimer::singleShot(1500, this, [this]() { refreshModels(); });
}

/* -------------------------------------------------------------------- */
/** \name Threads
 * \{ */

QString Studio::threadsDir() const
{
  const QString d = Profiles::stateDir() + "/threads/" + (profiles_ ? profiles_->currentId() : "x");
  QDir().mkpath(d);
  return d;
}

QVariantList Studio::threads() const
{
  QVariantList out;
  QDir dir(threadsDir());
  const auto files = dir.entryInfoList({"*.json"}, QDir::Files, QDir::Time);
  for (const QFileInfo &fi : files) {
    QFile f(fi.filePath());
    if (!f.open(QIODevice::ReadOnly)) continue;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    out << QVariantMap{{"id", fi.completeBaseName()},
                       {"title", o["title"].toString()},
                       {"updated", fi.lastModified().toString("d MMM, HH:mm")}};
  }
  return out;
}

void Studio::loadThreads()
{
  Q_EMIT threadsChanged();
}

QJsonObject Studio::projectThreads() const
{
  QFile f(threadsDir() + "/projects.map");
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

void Studio::rememberProjectThread()
{
  if (current_project_.isEmpty() || thread_id_.isEmpty() || messages_.isEmpty()) return;
  QJsonObject map = projectThreads();
  if (map[current_project_].toString() == thread_id_) return;
  map[current_project_] = thread_id_;
  QSaveFile f(threadsDir() + "/projects.map");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(map).toJson(QJsonDocument::Compact));
    f.commit();
  }
}

void Studio::projectChanged(const QString &path)
{
  if (project_known_ && path == current_project_) return;
  const bool first = !project_known_;
  const QString before = current_project_;
  project_known_ = true;
  current_project_ = path;
  if (path.isEmpty()) {
    if (!first && !before.isEmpty()) newThread();   /* a new, unsaved file */
    return;
  }
  const QString id = projectThreads()[path].toString();
  if (!id.isEmpty() && QFile::exists(threadsDir() + "/" + id + ".json")) {
    if (id != thread_id_) openThread(id);         /* the conversation of this project */
    return;
  }
  if (before.isEmpty() && !messages_.isEmpty()) {
    rememberProjectThread();                     /* "Save as": the talk so far belongs to it */
    return;
  }
  if (!first) newThread();
}

void Studio::saveThread()
{
  if (thread_id_.isEmpty() || messages_.isEmpty()) return;
  rememberProjectThread();
  QFile f(threadsDir() + "/" + thread_id_ + ".json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{"title", thread_title_},
                                      {"messages", QJsonArray::fromVariantList(messages_)},
                                      {"history", history_}})
                .toJson(QJsonDocument::Compact));
  }
  Q_EMIT threadsChanged();
}

void Studio::dumpThread(const QString &path) const
{
  QFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{"provider", agentProvider()},
                                      {"model", agentModel()},
                                      {"messages", QJsonArray::fromVariantList(messages_)},
                                      {"history", history_}})
                .toJson(QJsonDocument::Indented));
  }
}

void Studio::newThread()
{
  if (busy_) stop();
  thread_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
  thread_title_.clear();
  messages_.clear();
  history_ = QJsonArray();
  Q_EMIT threadChanged();
  Q_EMIT messagesChanged();
}

void Studio::openThread(const QString &id)
{
  if (busy_) stop();
  QFile f(threadsDir() + "/" + id + ".json");
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  thread_id_ = id;
  thread_title_ = o["title"].toString();
  messages_ = o["messages"].toArray().toVariantList();
  history_ = o["history"].toArray();
  /* An approval left open when the app closed is void now. */
  for (QVariant &v : messages_) {
    QVariantMap m = v.toMap();
    if (m["role"] == "approval" && m["state"] == "pending") {
      m["state"] = "expired";
      v = m;
    }
    if (m["streaming"].toBool()) {
      m["streaming"] = false;
      v = m;
    }
  }
  Q_EMIT threadChanged();
  Q_EMIT messagesChanged();
}

void Studio::deleteThread(const QString &id)
{
  QFile::remove(threadsDir() + "/" + id + ".json");
  if (id == thread_id_) newThread();
  Q_EMIT threadsChanged();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Messages
 * \{ */

void Studio::appendMessage(const QVariantMap &m)
{
  messages_ << m;
  Q_EMIT messagesChanged();
}

void Studio::updateLastAssistant(const QString &text, bool streaming)
{
  /* Only the answer being written right now is updated: the last row, when it is an answer.
   * After tool steps a new answer is appended below them (updating an earlier answer moved
   * the final reply above the steps that led to it). */
  if (!messages_.isEmpty()) {
    QVariantMap m = messages_.last().toMap();
    if (m["role"] == "assistant") {
      m["text"] = text;
      m["streaming"] = streaming;
      messages_.last() = m;
      Q_EMIT messagesChanged();
      return;
    }
  }
  appendMessage({{"role", "assistant"}, {"text", text}, {"streaming", streaming}});
}

void Studio::setBusy(bool b, const QString &activity, const QString &phase, const QString &tool)
{
  const QString new_phase = b ? phase : QString();
  if (new_phase != activity_phase_ || tool != activity_tool_ || !b) {
    activity_since_ = QDateTime::currentMSecsSinceEpoch();
  }
  busy_ = b;
  activity_ = activity;
  activity_phase_ = new_phase;
  activity_tool_ = b ? tool : QString();
  Q_EMIT busyChanged();
}

int Studio::activityMaxSteps() const
{
  return profiles_ ? profiles_->get("agent.max_steps").toInt() : 40;
}

void Studio::failTurn(const QString &message)
{
  appendMessage({{"role", "notice"}, {"kind", "error"}, {"text", message}});
  setBusy(false);
  saveThread();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Prompt & tools
 * \{ */

QString Studio::systemPrompt() const
{
  return QStringLiteral(
      "You are the scene agent inside Harness Studio, a Blender %1 based 3D production app used "
      "by artists, agencies and creative teams. You see and change the open Blender session "
      "through tools, and you carry a task through to the end on your own.\n\n"
      "How to work:\n"
      "- Start with one short sentence saying what you will do; finish with a brief summary of "
      "what changed.\n"
      "- Inspect before you change: call blender_scene_summary (and blender_object_info) when "
      "you are unsure what exists. After changes that affect the look, verify with "
      "blender_capture_viewport.\n"
      "- Make changes with blender_execute_python. The code runs on Blender's main thread with "
      "the main 3D viewport as context; bpy.ops that need a viewport work. Prefer bpy.data and "
      "direct property access. Put values you need back into a dict named `result`; printed "
      "output is returned too. Each call is one undo step.\n"
      "- Blender facts: Z is up, units are metres, rotations in radians in the API (convert from "
      "degrees with math.radians). Use the Principled BSDF for materials (inputs 'Base Color', "
      "'Metallic', 'Roughness', 'Emission Color', 'Emission Strength', 'Transmission Weight'). "
      "Name new objects meaningfully. Never delete or overwrite the user's data unless asked.\n"
      "- This is Blender 5 (layered actions); action.fcurves still lists an action's F-curves "
      "(read-only). Your code also has Vector, Matrix, Euler, Quaternion, math and the helpers "
      "fcurves(obj), set_interpolation(obj, 'LINEAR'|'BEZIER'|'CONSTANT') and "
      "set_engine('EEVEE'|'CYCLES'|'WORKBENCH'). A failed call is rolled back completely, so "
      "redo the whole step after fixing it.\n"
      "- A failed tool call is normal: read the error, fix the cause, try another approach. "
      "Never repeat an identical failing call.\n"
      "- generate_image, generate_video and generate_3d call paid models with the user's own "
      "keys; use them only when the task needs generated media, and say what you generate. "
      "3D results are imported into the scene automatically.\n"
      "- The user can point at scene items with @Name. Their live data comes with the message; "
      "treat them as the targets.\n"
      "- Scene link tools connect the scene to generation: blender_capture_pass gives a depth, "
      "normal, edge or mask image of the viewport or camera; blender_camera_path describes the "
      "camera move; generate_image/generate_video accept reference_pass, follow_camera and "
      "apply_material_to; generate_3d accepts place and target; blender_apply_image_material "
      "dresses objects in a generated image; blender_place_objects puts objects at the 3D cursor "
      "or on another object; blender_match_lighting matches the lights to an image.\n"
      "- Keep replies short and concrete. Reply in the user's language.\n\n"
      "Current scene: %2 objects, mode %3, active object: %4, file: %5.")
      .arg(Bridge::instance()->version(),
           QString::number(Bridge::instance()->objectCount()),
           Bridge::instance()->mode(),
           Bridge::instance()->activeObject().isEmpty() ? QStringLiteral("none")
                                                        : Bridge::instance()->activeObject(),
           Bridge::instance()->projectName().isEmpty() ? QStringLiteral("unsaved")
                                                       : Bridge::instance()->projectName());
}

QJsonArray Studio::toolSchemas(bool anthropic) const
{
  auto obj = [](const QJsonObject &props, const QJsonArray &required) {
    return QJsonObject{{"type", "object"}, {"properties", props}, {"required", required}};
  };
  auto str = [](const QString &d) { return QJsonObject{{"type", "string"}, {"description", d}}; };
  struct T {
    QString name, desc;
    QJsonObject schema;
  };
  const std::vector<T> tools = {
      {"blender_scene_summary",
       "List the scene: objects with type, transform, dimensions, materials, modifiers, "
       "collections, camera, render settings and the current mode.",
       obj({}, {})},
      {"blender_object_info", "Full detail of one object by name.",
       obj({{"name", str("Object name")}}, {"name"})},
      {"blender_execute_python",
       "Run Python (bpy) in the open Blender session. Put returned values into a dict named "
       "`result`. One undo step per call.",
       obj({{"code", str("Python source")},
            {"description", str("Short undo label, e.g. 'Add studio lights'")}},
           {"code"})},
      {"blender_capture_viewport",
       "Capture the main 3D viewport as an image you can look at.", obj({}, {})},
      {"blender_render_image",
       "Render the scene camera with the current render settings and return the image.",
       obj({}, {})},
      {"blender_set_workspace", "Switch the main window to a workspace (Layout, Modeling...).",
       obj({{"name", str("Workspace name")}}, {"name"})},
      {"blender_import_file",
       "Import a file (glb, gltf, fbx, obj, usd, stl, ply, abc, bvh, images) into the scene.",
       obj({{"path", str("Absolute file path")},
            {"as", QJsonObject{{"type", "string"},
                               {"enum", QJsonArray{"object", "texture", "world"}},
                               {"description", "For images: plane object, texture on the "
                                               "active mesh, or world environment"}}}},
           {"path"})},
      {"blender_capture_pass",
       "Capture a reference image of the scene: beauty, depth (near = white), normal, edges or an "
       "object mask, from the viewport's angle or the scene camera.",
       obj({{"pass", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"beauty", "depth", "normal", "edges", "mask"}}}},
            {"source", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"viewport", "camera"}}}},
            {"objects", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                    {"description", "Objects for the mask pass"}}}},
           {"pass"})},
      {"blender_camera_path",
       "Describe the scene camera's animation over the frame range: samples (location, rotation, "
       "lens per frame) and the move in words (dolly, truck, pan, tilt, zoom).",
       obj({}, {})},
      {"blender_apply_image_material",
       "Dress objects in a PBR material made from an image (base colour, roughness and bump from "
       "it, box-projected so no UVs are needed).",
       obj({{"image", str("Absolute path of the image (e.g. a generated texture)")},
            {"objects", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}}},
           {"image", "objects"})},
      {"blender_place_objects",
       "Move objects to the 3D cursor, onto another object (resting on top, sized to it) or to the "
       "origin, standing on the surface below.",
       obj({{"objects", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
            {"at", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"cursor", "object", "origin"}}}},
            {"target", str("Object to place on when at = object")}},
           {"objects", "at"})},
      {"blender_match_lighting",
       "Match the scene's world colour and a key light (colour, direction) to an image.",
       obj({{"image", str("Absolute path of the image")}}, {"image"})},
      {"generate_image", "Generate an image with the user's fal.ai key (paid).",
       obj({{"prompt", str("What to draw")},
            {"use_viewport", QJsonObject{{"type", "boolean"},
                                         {"description", "Use the current viewport as reference"}}},
            {"reference_pass", QJsonObject{{"type", "string"},
                                           {"enum", QJsonArray{"beauty", "depth", "normal", "edges", "mask"}},
                                           {"description", "Which scene pass to send as the reference image"}}},
            {"apply_material_to", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                              {"description", "Objects to dress in the result as a material"}}}},
           {"prompt"})},
      {"generate_video", "Generate a short video with the user's fal.ai key (paid).",
       obj({{"prompt", str("What happens")},
            {"use_viewport", QJsonObject{{"type", "boolean"},
                                         {"description", "Start from the current viewport"}}},
            {"follow_camera", QJsonObject{{"type", "boolean"},
                                          {"description", "Follow the scene camera's animation (start/end frames and the move)"}}}},
           {"prompt"})},
      {"generate_3d",
       "Generate a 3D model with the user's fal.ai key (paid); it is imported into the scene.",
       obj({{"prompt", str("The object to make")},
            {"use_viewport", QJsonObject{{"type", "boolean"},
                                         {"description", "Use the viewport image as reference"}}},
            {"place", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"cursor", "object", "origin"}}}},
            {"target", str("Object to place it on when place = object")}},
           {"prompt"})},
  };
  QJsonArray out;
  for (const T &t : tools) {
    if (anthropic) {
      out << QJsonObject{{"name", t.name}, {"description", t.desc}, {"input_schema", t.schema}};
    }
    else {
      out << QJsonObject{{"type", "function"},
                         {"function", QJsonObject{{"name", t.name},
                                                  {"description", t.desc},
                                                  {"parameters", t.schema}}}};
    }
  }
  return out;
}

/** Images that go with a user message: the viewport capture and the person's attachments. */
static QStringList user_images(const QJsonObject &m)
{
  QStringList out;
  if (!m["image"].toString().isEmpty()) out << m["image"].toString();
  for (const auto &v : m["images"].toArray()) {
    if (!v.toString().isEmpty()) out << v.toString();
  }
  return out;
}

static QString image_data_base64(const QString &path, QString &media_type)
{
  QImage img(path);
  if (img.isNull()) return QString();
  if (img.width() > 1280) img = img.scaledToWidth(1280, Qt::SmoothTransformation);
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPEG", 88);
  media_type = "image/jpeg";
  return QString::fromLatin1(bytes.toBase64());
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Turn
 * \{ */

void Studio::send(const QString &text, bool attach_viewport, const QVariantList &refs,
                  const QStringList &attachments)
{
  const QString t = text.trimmed();
  if ((t.isEmpty() && attachments.isEmpty()) || busy_) return;
  QStringList files;
  QStringList urls;
  for (const QString &a : attachments) {
    const QString path = QUrl(a).isLocalFile() ? QUrl(a).toLocalFile() : a;
    if (QFile::exists(path)) {
      files << path;
      urls << QUrl::fromLocalFile(path).toString();
    }
  }
  const QString provider = agentProvider();
  if (provider != "ollama" && key(provider).isEmpty()) {
    appendMessage({{"role", "user"}, {"text", t}, {"refs", refs}, {"images", urls}});
    failTurn(tr("Add your %1 API key in Settings → AI Providers, or pick another model.")
                 .arg(providerName(provider)));
    return;
  }
  if (thread_title_.isEmpty()) {
    thread_title_ = t.left(60);
  }
  appendMessage({{"role", "user"}, {"text", t}, {"refs", refs}, {"images", urls}});
  steps_ = 0;
  recent_tool_signatures_.clear();
  /* The model gets the text, the live detail of every @ reference, and the viewport. */
  auto proceed = [this, t, files, urls](const QString &context, const QString &image) {
    QString content = t.isEmpty() ? tr("(see the attached images)") : t;
    if (!context.isEmpty()) {
      content += QStringLiteral("\n\n[Scene references: live data from Blender for the @ mentions above. "
                                "They are the targets of this request.]\n") + context;
    }
    QJsonObject msg{{"role", "user"}, {"content", content}};
    if (!files.isEmpty()) {
      msg["images"] = QJsonArray::fromStringList(files);
    }
    if (!image.isEmpty()) {
      msg["image"] = image;
      QVariantMap last = messages_.last().toMap();
      last["images"] = QStringList(urls) << QUrl::fromLocalFile(image).toString();
      messages_.last() = last;
      Q_EMIT messagesChanged();
    }
    history_.append(msg);
    step();
  };
  setBusy(true, tr("Thinking"));
  auto with_image = [this, attach_viewport, proceed](const QString &context) {
    if (attach_viewport) {
      captureViewport([proceed, context](QString path, QString) { proceed(context, path); });
    }
    else {
      proceed(context, QString());
    }
  };
  if (refs.isEmpty()) {
    with_image(QString());
    return;
  }
  setBusy(true, tr("Reading the referenced objects"), QStringLiteral("reading"));
  Bridge::instance()->request("ref_context", {{"refs", QJsonArray::fromVariantList(refs)}},
                              [with_image](const QJsonObject &r) {
                                with_image(QString::fromUtf8(
                                    QJsonDocument(r["references"].toArray()).toJson(QJsonDocument::Compact)));
                              });
}

void Studio::retry()
{
  if (busy_ || history_.isEmpty()) return;
  /* Drop the last answer and its notices, then ask again from the last user message. */
  while (!history_.isEmpty() && history_.last().toObject()["role"].toString() != "user") {
    history_.removeLast();
  }
  while (!messages_.isEmpty() && messages_.last().toMap()["role"].toString() != "user") {
    messages_.removeLast();
  }
  Q_EMIT messagesChanged();
  if (history_.isEmpty()) return;
  steps_ = 0;
  setBusy(true, tr("Thinking"));
  step();
}

void Studio::stop()
{
  if (reply_) {
    reply_->abort();
  }
  pending_calls_.clear();
  for (auto &[idx, cb] : approvals_) {
    if (idx < messages_.size()) {
      QVariantMap m = messages_[idx].toMap();
      m["state"] = "expired";
      messages_[idx] = m;
    }
  }
  approvals_.clear();
  for (QVariant &v : messages_) {
    QVariantMap m = v.toMap();
    if (m["streaming"].toBool()) {
      m["streaming"] = false;
      v = m;
    }
  }
  Q_EMIT messagesChanged();
  if (busy_) {
    appendMessage({{"role", "notice"}, {"kind", "info"}, {"text", tr("Stopped.")}});
  }
  setBusy(false);
  saveThread();
}

/** An API error as a person can act on it: the message, and for routers like OpenRouter the
 * upstream provider and its own words (metadata.raw), which carry the real reason. */
static QString describe_api_error(const QJsonValue &v)
{
  if (v.isString()) return v.toString();
  const QJsonObject e = v.toObject();
  QString msg = e["message"].toString();
  const QJsonObject meta = e["metadata"].toObject();
  QString raw = meta["raw"].isString() ? meta["raw"].toString()
                                       : QString::fromUtf8(QJsonDocument(meta["raw"].toObject()).toJson(QJsonDocument::Compact));
  if (raw == "{}") raw.clear();
  const QJsonObject inner = QJsonDocument::fromJson(raw.toUtf8()).object();
  if (!inner.isEmpty()) {
    const QJsonValue ie = inner["error"];
    const QString im = ie.isObject() ? ie.toObject()["message"].toString() : ie.toString();
    if (!im.isEmpty()) raw = im;
  }
  const QString who = meta["provider_name"].toString();
  if (!raw.isEmpty() || !who.isEmpty()) {
    msg += QStringLiteral(" (") + (who.isEmpty() ? QString() : who + QStringLiteral(": ")) + raw.left(240) + QStringLiteral(")");
  }
  if (e.contains("code") && !msg.contains(e["code"].toVariant().toString())) {
    msg += QStringLiteral(" [") + e["code"].toVariant().toString() + QStringLiteral("]");
  }
  return msg;
}

bool Studio::retryAfterError(int http_status, const QString &message)
{
  /* Rate limits, overloaded or failing upstream providers are usually gone a moment later:
   * try the same request twice more, waiting a little longer each time. */
  const bool transient = http_status == 429 || http_status >= 500 || http_status == 0 ||
                         message.contains("protocol error", Qt::CaseInsensitive) ||
                         message.contains("connection", Qt::CaseInsensitive) ||
                         message.contains("Provider returned error", Qt::CaseInsensitive) ||
                         message.contains("overloaded", Qt::CaseInsensitive) ||
                         message.contains("rate limit", Qt::CaseInsensitive) ||
                         message.contains("timeout", Qt::CaseInsensitive);
  if (!transient || retries_ >= 2) return false;
  retries_++;
  /* Text that had already arrived in this attempt goes away: the retry writes it again. */
  if (!stream_text_.isEmpty() && !messages_.isEmpty()) {
    const QVariantMap last = messages_.last().toMap();
    if (last["role"] == "assistant" && last["streaming"].toBool()) {
      messages_.removeLast();
      Q_EMIT messagesChanged();
    }
  }
  setBusy(true, tr("The provider didn't answer; trying again (%1 of 2)").arg(retries_), QStringLiteral("thinking"));
  QTimer::singleShot(1500 * retries_, this, [this]() {
    if (!busy_) return;
    stream_text_.clear();
    sse_buffer_.clear();
    stream_event_.clear();
    pending_calls_.clear();
    stream_tool_index_.clear();
    if (agentProvider() == "anthropic") requestAnthropic();
    else requestOpenAICompatible();
  });
  return true;
}

void Studio::step()
{
  retries_ = 0;
  const int max_steps = profiles_ ? profiles_->get("agent.max_steps").toInt() : 40;
  if (++steps_ > max_steps) {
    failTurn(tr("Stopped after %1 steps. Say \"continue\" to let the agent go on.").arg(max_steps));
    return;
  }
  stream_text_.clear();
  sse_buffer_.clear();
  stream_event_.clear();
  pending_calls_.clear();
  stream_tool_index_.clear();
  setBusy(true, steps_ == 1 ? tr("Thinking") : tr("Deciding the next step"), QStringLiteral("thinking"));
  if (agentProvider() == "anthropic") requestAnthropic();
  else requestOpenAICompatible();
}

void Studio::requestAnthropic()
{
  QJsonArray msgs;
  QJsonArray pending_results;
  auto flush_results = [&]() {
    if (!pending_results.isEmpty()) {
      msgs << QJsonObject{{"role", "user"}, {"content", pending_results}};
      pending_results = QJsonArray();
    }
  };
  for (const auto &v : history_) {
    const QJsonObject m = v.toObject();
    const QString role = m["role"].toString();
    if (role == "tool") {
      QJsonArray content{QJsonObject{{"type", "text"}, {"text", m["content"].toString()}}};
      if (!m["image"].toString().isEmpty()) {
        QString mt;
        const QString data = image_data_base64(m["image"].toString(), mt);
        if (!data.isEmpty()) {
          content << QJsonObject{{"type", "image"},
                                 {"source", QJsonObject{{"type", "base64"},
                                                        {"media_type", mt},
                                                        {"data", data}}}};
        }
      }
      pending_results << QJsonObject{{"type", "tool_result"},
                                     {"tool_use_id", m["tool_call_id"]},
                                     {"content", content},
                                     {"is_error", m["is_error"].toBool()}};
      continue;
    }
    flush_results();
    if (role == "user") {
      QJsonArray content;
      for (const QString &img : user_images(m)) {
        QString mt;
        const QString data = image_data_base64(img, mt);
        if (!data.isEmpty()) {
          content << QJsonObject{{"type", "image"},
                                 {"source", QJsonObject{{"type", "base64"},
                                                        {"media_type", mt},
                                                        {"data", data}}}};
        }
      }
      content << QJsonObject{{"type", "text"}, {"text", m["content"].toString()}};
      msgs << QJsonObject{{"role", "user"}, {"content", content}};
    }
    else if (role == "assistant") {
      QJsonArray content;
      if (!m["content"].toString().isEmpty()) {
        content << QJsonObject{{"type", "text"}, {"text", m["content"].toString()}};
      }
      for (const auto &tc : m["tool_calls"].toArray()) {
        const QJsonObject c = tc.toObject();
        content << QJsonObject{{"type", "tool_use"},
                               {"id", c["id"]},
                               {"name", c["name"]},
                               {"input", QJsonDocument::fromJson(c["arguments"].toString().toUtf8()).object()}};
      }
      if (!content.isEmpty()) msgs << QJsonObject{{"role", "assistant"}, {"content", content}};
    }
  }
  flush_results();

  QJsonObject body{{"model", agentModel()},
                   {"max_tokens", 8192},
                   {"system", systemPrompt()},
                   {"messages", msgs},
                   {"tools", toolSchemas(true)},
                   {"stream", true}};
  QNetworkRequest req(QUrl("https://api.anthropic.com/v1/messages"));
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("x-api-key", key("anthropic").toUtf8());
  req.setRawHeader("anthropic-version", "2023-06-01");
  req.setRawHeader("Accept", "text/event-stream");
  req.setTransferTimeout(600000);
  /* Long server-sent-event streams are steadier over HTTP/1.1 (HTTP/2 streams were cut with
   * "protocol error" by some providers' proxies). */
  req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
  reply_ = net_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
  QNetworkReply *rep = reply_;
  connect(rep, &QNetworkReply::readyRead, this, [this, rep]() {
    if (rep == reply_) handleStreamChunk(rep->readAll());
  });
  connect(rep, &QNetworkReply::finished, this, [this, rep]() {
    rep->deleteLater();
    if (rep != reply_) return;
    if (rep->error() == QNetworkReply::OperationCanceledError) return;
    const QByteArray rest = rep->readAll();
    if (rep->error() != QNetworkReply::NoError) {
      const QJsonObject err = QJsonDocument::fromJson(sse_buffer_ + rest).object();
      const QString msg = describe_api_error(err["error"]);
      const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (retryAfterError(status, msg)) return;
      failTurn(tr("Anthropic: %1").arg(msg.isEmpty() ? rep->errorString() : msg));
      return;
    }
    if (!rest.isEmpty()) handleStreamChunk(rest);
    finishStep(stream_event_);
  });
}

void Studio::requestOpenAICompatible()
{
  const QString provider = agentProvider();
  const QString model = agentModel();
  bool vision = true;
  for (const auto &m : models_[provider]) {
    if (m.toObject()["id"].toString() == model) vision = m.toObject()["vision"].toBool(true);
  }
  QJsonArray msgs{QJsonObject{{"role", "system"}, {"content", systemPrompt()}}};
  QStringList tool_images;
  auto flush_images = [&]() {
    if (tool_images.isEmpty() || !vision) {
      tool_images.clear();
      return;
    }
    QJsonArray content{QJsonObject{{"type", "text"}, {"text", "Image returned by the tool call above."}}};
    for (const QString &p : tool_images) {
      QString mt;
      const QString data = image_data_base64(p, mt);
      if (!data.isEmpty()) {
        content << QJsonObject{{"type", "image_url"},
                               {"image_url", QJsonObject{{"url", "data:" + mt + ";base64," + data}}}};
      }
    }
    msgs << QJsonObject{{"role", "user"}, {"content", content}};
    tool_images.clear();
  };
  for (const auto &v : history_) {
    const QJsonObject m = v.toObject();
    const QString role = m["role"].toString();
    if (role != "tool") flush_images();
    if (role == "user") {
      const QStringList imgs = user_images(m);
      if (!imgs.isEmpty() && vision) {
        QJsonArray content{QJsonObject{{"type", "text"}, {"text", m["content"]}}};
        for (const QString &img : imgs) {
          QString mt;
          const QString data = image_data_base64(img, mt);
          if (!data.isEmpty()) {
            content << QJsonObject{{"type", "image_url"},
                                   {"image_url", QJsonObject{{"url", "data:" + mt + ";base64," + data}}}};
          }
        }
        msgs << QJsonObject{{"role", "user"}, {"content", content}};
      }
      else {
        msgs << QJsonObject{{"role", "user"}, {"content", m["content"]}};
      }
    }
    else if (role == "assistant") {
      QJsonObject a{{"role", "assistant"}, {"content", m["content"].toString()}};
      QJsonArray calls;
      for (const auto &tc : m["tool_calls"].toArray()) {
        const QJsonObject c = tc.toObject();
        calls << QJsonObject{{"id", c["id"]},
                             {"type", "function"},
                             {"function", QJsonObject{{"name", c["name"]}, {"arguments", c["arguments"]}}}};
      }
      if (!calls.isEmpty()) a["tool_calls"] = calls;
      msgs << a;
    }
    else if (role == "tool") {
      msgs << QJsonObject{{"role", "tool"}, {"tool_call_id", m["tool_call_id"]}, {"content", m["content"]}};
      if (!m["image"].toString().isEmpty()) tool_images << m["image"].toString();
    }
  }
  flush_images();

  QJsonObject body{{"model", model}, {"messages", msgs}, {"tools", toolSchemas(false)}, {"stream", true}};
  const QString lm = model.toLower();
  const bool reasoning = provider == "openai" &&
                         (lm.startsWith("o") || lm.startsWith("gpt-5") || lm.startsWith("gpt-6"));
  if (!reasoning) {
    body["temperature"] = profiles_ ? profiles_->get("agent.temperature").toDouble() : 0.4;
  }
  if (provider == "openai" || provider == "openrouter") {
    body["stream_options"] = QJsonObject{{"include_usage", true}};
  }
  QNetworkRequest req(QUrl(chatBase(provider) + "/chat/completions"));
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("Accept", "text/event-stream");
  if (provider != "ollama") {
    req.setRawHeader("Authorization", "Bearer " + key(provider).toUtf8());
  }
  if (provider == "openrouter") {
    req.setRawHeader("HTTP-Referer", "https://harness.studio");
    req.setRawHeader("X-Title", "Harness Studio");
  }
  req.setTransferTimeout(600000);
  /* Long server-sent-event streams are steadier over HTTP/1.1 (HTTP/2 streams were cut with
   * "protocol error" by some providers' proxies). */
  req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
  reply_ = net_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
  QNetworkReply *rep = reply_;
  connect(rep, &QNetworkReply::readyRead, this, [this, rep]() {
    if (rep == reply_) handleStreamChunk(rep->readAll());
  });
  connect(rep, &QNetworkReply::finished, this, [this, rep, provider]() {
    rep->deleteLater();
    if (rep != reply_) return;
    if (rep->error() == QNetworkReply::OperationCanceledError) return;
    const QByteArray rest = rep->readAll();
    if (rep->error() != QNetworkReply::NoError) {
      const QJsonObject err = QJsonDocument::fromJson(sse_buffer_ + rest).object();
      const QString msg = describe_api_error(err["error"]);
      const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (retryAfterError(status, msg)) return;
      QString hint;
      if (provider == "openrouter" && agentModel().endsWith(":free")) {
        hint = tr(" Free models are often busy: the Free router (openrouter/free) picks one that is up.");
      }
      failTurn(tr("%1: %2").arg(providerName(provider), msg.isEmpty() ? rep->errorString() : msg) + hint);
      return;
    }
    if (!rest.isEmpty()) handleStreamChunk(rest);
    finishStep(stream_event_);
  });
}

void Studio::handleStreamChunk(const QByteArray &chunk)
{
  sse_buffer_ += chunk;
  const bool anthropic = agentProvider() == "anthropic";
  int sep;
  while ((sep = sse_buffer_.indexOf("\n\n")) >= 0 || (sep = sse_buffer_.indexOf("\r\n\r\n")) >= 0) {
    const QByteArray event = sse_buffer_.left(sep);
    sse_buffer_.remove(0, sep + (sse_buffer_.mid(sep, 4) == "\r\n\r\n" ? 4 : 2));
    QByteArray data;
    for (const QByteArray &line : event.split('\n')) {
      const QByteArray l = line.trimmed();
      if (l.startsWith("data:")) data += l.mid(5).trimmed();
    }
    if (data.isEmpty() || data == "[DONE]") continue;
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    if (anthropic) {
      const QString type = o["type"].toString();
      if (type == "message_start") {
        input_tokens_ = o["message"].toObject()["usage"].toObject()["input_tokens"].toInt();
      }
      else if (type == "content_block_start") {
        const QJsonObject b = o["content_block"].toObject();
        if (b["type"].toString() == "tool_use") {
          stream_tool_index_[o["index"].toInt()] = int(pending_calls_.size());
          pending_calls_.push_back({b["id"].toString(), b["name"].toString(), QString(), {}});
        }
      }
      else if (type == "content_block_delta") {
        const QJsonObject d = o["delta"].toObject();
        if (d["type"].toString() == "text_delta") {
          stream_text_ += d["text"].toString();
          if (stream_text_.trimmed().isEmpty()) continue;
          updateLastAssistant(stream_text_, true);
          if (activity_phase_ != QLatin1String("writing")) setBusy(true, tr("Writing the answer"), QStringLiteral("writing"));
        }
        else if (d["type"].toString() == "input_json_delta") {
          auto it = stream_tool_index_.find(o["index"].toInt());
          if (it != stream_tool_index_.end()) {
            pending_calls_[it->second].args_json += d["partial_json"].toString();
          }
        }
      }
      else if (type == "message_delta") {
        stream_event_ = o["delta"].toObject()["stop_reason"].toString();
        output_tokens_ = o["usage"].toObject()["output_tokens"].toInt();
      }
      else if (type == "error") {
        stream_event_ = "error:" + describe_api_error(o["error"]);
      }
    }
    else {
      if (o.contains("error")) {
        stream_event_ = "error:" + describe_api_error(o["error"]);
        continue;
      }
      if (o.contains("usage") && o["usage"].isObject()) {
        input_tokens_ = o["usage"].toObject()["prompt_tokens"].toInt();
        output_tokens_ = o["usage"].toObject()["completion_tokens"].toInt();
      }
      for (const auto &cv : o["choices"].toArray()) {
        const QJsonObject c = cv.toObject();
        const QJsonObject d = c["delta"].toObject();
        if (d["content"].isString() && !d["content"].toString().isEmpty()) {
          stream_text_ += d["content"].toString();
          if (stream_text_.trimmed().isEmpty()) continue;   /* no empty bubbles for whitespace */
          updateLastAssistant(stream_text_, true);
          if (activity_phase_ != QLatin1String("writing")) setBusy(true, tr("Writing the answer"), QStringLiteral("writing"));
        }
        for (const auto &tv : d["tool_calls"].toArray()) {
          const QJsonObject t = tv.toObject();
          const int idx = t["index"].toInt();
          auto it = stream_tool_index_.find(idx);
          if (it == stream_tool_index_.end()) {
            stream_tool_index_[idx] = int(pending_calls_.size());
            pending_calls_.push_back({t["id"].toString(), QString(), QString(), {}});
            it = stream_tool_index_.find(idx);
          }
          ToolCall &call = pending_calls_[it->second];
          if (!t["id"].toString().isEmpty()) call.id = t["id"].toString();
          const QJsonObject f = t["function"].toObject();
          if (!f["name"].toString().isEmpty()) call.name += f["name"].toString();
          call.args_json += f["arguments"].toString();
        }
        if (!c["finish_reason"].isNull() && c["finish_reason"].isString()) {
          stream_event_ = c["finish_reason"].toString();
        }
      }
    }
  }
}

void Studio::finishStep(const QString &stop_reason)
{
  if (profiles_ && (input_tokens_ || output_tokens_)) {
    profiles_->addUsage(agentProvider(), input_tokens_, output_tokens_, 0.0);
  }
  input_tokens_ = output_tokens_ = 0;
  if (stop_reason.startsWith("error:")) {
    if (retryAfterError(200, stop_reason.mid(6))) return;
    failTurn(stop_reason.mid(6));
    return;
  }
  if (!stream_text_.isEmpty()) {
    updateLastAssistant(stream_text_, false);
  }
  /* Parse tool arguments; tolerate an empty object. */
  QJsonArray calls;
  for (ToolCall &c : pending_calls_) {
    if (c.id.isEmpty()) c.id = "call_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QJsonDocument doc = QJsonDocument::fromJson(c.args_json.toUtf8());
    c.args = doc.isObject() ? doc.object() : QJsonObject();
    calls << QJsonObject{{"id", c.id},
                         {"name", c.name},
                         {"arguments", QString::fromUtf8(QJsonDocument(c.args).toJson(QJsonDocument::Compact))}};
  }
  QJsonObject assistant{{"role", "assistant"}, {"content", stream_text_}};
  if (!calls.isEmpty()) assistant["tool_calls"] = calls;
  history_.append(assistant);
  if (pending_calls_.empty()) {
    setBusy(false);
    saveThread();
    return;
  }
  runTools();
}

static QString tool_label(const ToolCall &c)
{
  const QString n = c.name;
  if (n == "blender_scene_summary") return QObject::tr("Read the scene");
  if (n == "blender_object_info") return QObject::tr("Inspect %1").arg(c.args["name"].toString());
  if (n == "blender_execute_python") {
    const QString d = c.args["description"].toString();
    return d.isEmpty() ? QObject::tr("Run Python in Blender") : d;
  }
  if (n == "blender_capture_viewport") return QObject::tr("Look at the viewport");
  if (n == "blender_render_image") return QObject::tr("Render the camera");
  if (n == "blender_set_workspace") return QObject::tr("Switch to %1").arg(c.args["name"].toString());
  if (n == "blender_import_file") return QObject::tr("Import %1").arg(QFileInfo(c.args["path"].toString()).fileName());
  if (n == "blender_capture_pass") return QObject::tr("Capture the %1 pass").arg(c.args["pass"].toString());
  if (n == "blender_camera_path") return QObject::tr("Read the camera path");
  if (n == "blender_apply_image_material") return QObject::tr("Apply a material");
  if (n == "blender_place_objects") return QObject::tr("Place objects");
  if (n == "blender_match_lighting") return QObject::tr("Match the lighting");
  if (n == "generate_image") return QObject::tr("Generate an image");
  if (n == "generate_video") return QObject::tr("Generate a video");
  if (n == "generate_3d") return QObject::tr("Generate a 3D model");
  return n;
}

void Studio::runTools()
{
  if (pending_calls_.empty()) {
    step();
    return;
  }
  const ToolCall call = pending_calls_.front();
  pending_calls_.erase(pending_calls_.begin());

  /* Loop guard: the same call three times in a row is a circle. */
  const QString sig = call.name + call.args_json;
  recent_tool_signatures_ << sig;
  if (recent_tool_signatures_.size() > 6) recent_tool_signatures_.removeFirst();
  if (recent_tool_signatures_.count(sig) >= 3) {
    history_.append(QJsonObject{{"role", "tool"}, {"tool_call_id", call.id}, {"name", call.name},
                                {"content", "{\"error\":\"Identical call repeated; stopping.\"}"},
                                {"is_error", true}});
    failTurn(tr("The agent repeated the same step three times and was stopped."));
    return;
  }

  QVariantMap row{{"role", "tool"},
                  {"tool", call.name},
                  {"text", tool_label(call)},
                  {"status", "running"},
                  {"detail", call.name == "blender_execute_python" ? call.args["code"].toString() : QString()}};
  appendMessage(row);
  const int row_index = int(messages_.size()) - 1;
  setBusy(true, tool_label(call), QStringLiteral("tool"), call.name);

  auto run = [this, call, row_index]() {
    runTool(call, [this, call, row_index](QJsonObject result, QString image) {
      const bool is_error = result.contains("error") && !result["error"].isNull();
      if (row_index < messages_.size()) {
        QVariantMap m = messages_[row_index].toMap();
        m["status"] = is_error ? "error" : "done";
        if (is_error) m["error"] = result["error"].toString().left(400);
        if (!image.isEmpty()) m["images"] = QStringList{QUrl::fromLocalFile(image).toString()};
        messages_[row_index] = m;
        Q_EMIT messagesChanged();
      }
      QString content = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
      if (content.size() > 16000) content = content.left(8000) + " …(truncated)… " + content.right(6000);
      QJsonObject tool{{"role", "tool"}, {"tool_call_id", call.id}, {"name", call.name},
                       {"content", content}, {"is_error", is_error}};
      if (!image.isEmpty()) tool["image"] = image;
      history_.append(tool);
      if (!busy_) return; /* Stopped meanwhile. */
      runTools();
    });
  };

  const bool paid = call.name.startsWith("generate_");
  const bool python = call.name == "blender_execute_python";
  const bool ask = (paid && profiles_ && profiles_->get("agent.approve_paid").toBool()) ||
                   (python && profiles_ && profiles_->get("agent.approve_python").toBool());
  if (!ask) {
    run();
    return;
  }
  appendMessage({{"role", "approval"},
                 {"text", paid ? tr("The agent wants to start a paid generation: %1")
                                     .arg(call.args["prompt"].toString())
                               : tr("The agent wants to run Python in Blender.")},
                 {"detail", python ? call.args["code"].toString() : QString()},
                 {"state", "pending"}});
  const int approval_index = int(messages_.size()) - 1;
  setBusy(true, tr("Waiting for your approval"), QStringLiteral("approval"));
  approvals_[approval_index] = [this, run, call, row_index](bool ok) {
    if (ok) {
      run();
      return;
    }
    if (row_index < messages_.size()) {
      QVariantMap m = messages_[row_index].toMap();
      m["status"] = "error";
      m["error"] = tr("Declined");
      messages_[row_index] = m;
      Q_EMIT messagesChanged();
    }
    history_.append(QJsonObject{{"role", "tool"}, {"tool_call_id", call.id}, {"name", call.name},
                                {"content", "{\"error\":\"The user declined this action.\"}"},
                                {"is_error", true}});
    runTools();
  };
}

void Studio::answerApproval(int message_index, bool approve)
{
  auto it = approvals_.find(message_index);
  if (it == approvals_.end()) return;
  auto cb = std::move(it->second);
  approvals_.erase(it);
  QVariantMap m = messages_[message_index].toMap();
  m["state"] = approve ? "approved" : "declined";
  messages_[message_index] = m;
  Q_EMIT messagesChanged();
  cb(approve);
}

void Studio::captureViewport(std::function<void(QString, QString)> done)
{
  const QString path = outputsDir() + QString("/viewport-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
  Bridge::instance()->request("capture_viewport", QJsonObject{{"path", path}},
                              [done, path](const QJsonObject &r) {
                                if (r.contains("error") || !QFile::exists(path)) {
                                  done(QString(), r["error"].toString("capture failed"));
                                }
                                else {
                                  done(path, QString());
                                }
                              });
}

void Studio::runTool(const ToolCall &call, std::function<void(QJsonObject, QString)> done)
{
  Bridge *b = Bridge::instance();
  const QString n = call.name;
  const QJsonObject a = call.args;
  if (n == "blender_scene_summary") {
    b->request("scene_summary", {}, [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_object_info") {
    b->request("object_info", {{"name", a["name"]}}, [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_execute_python") {
    b->request("exec", {{"code", a["code"]}, {"undo", a["description"].toString("Harness Agent")}},
               [done](const QJsonObject &r) {
                 QJsonObject out = r;
                 if (out["error"].isNull()) out.remove("error");
                 done(out, QString());
               });
  }
  else if (n == "blender_capture_viewport") {
    captureViewport([done](QString path, QString err) {
      if (path.isEmpty()) done(QJsonObject{{"error", err}}, QString());
      else done(QJsonObject{{"captured", true}}, path);
    });
  }
  else if (n == "blender_render_image") {
    const QString path = outputsDir() + QString("/render-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    b->request("render_still", {{"path", path}}, [done, path](const QJsonObject &r) {
      if (r.contains("error")) done(r, QString());
      else done(QJsonObject{{"rendered", path}}, path);
    });
  }
  else if (n == "blender_set_workspace") {
    b->request("workspace", {{"name", a["name"]}}, [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_import_file") {
    b->request("import_file", {{"path", a["path"]}, {"as", a["as"].toString("object")}},
               [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_capture_pass") {
    const QString pass = a["pass"].toString("depth");
    const QString path = outputsDir() + QString("/%1-%2.png").arg(pass).arg(QDateTime::currentMSecsSinceEpoch());
    QJsonArray refs;
    for (const auto &o : a["objects"].toArray()) refs << QJsonObject{{"kind", "object"}, {"name", o}};
    b->request("capture_pass", {{"path", path}, {"pass", pass}, {"source", a["source"].toString("viewport")},
                                {"refs", refs}, {"width", 1024}},
               [done, path](const QJsonObject &r) {
                 if (r.contains("error")) done(r, QString());
                 else done(QJsonObject{{"captured", path}}, path);
               });
  }
  else if (n == "blender_camera_path") {
    b->request("camera_path", {}, [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_apply_image_material") {
    QJsonArray refs;
    for (const auto &o : a["objects"].toArray()) refs << QJsonObject{{"kind", "object"}, {"name", o}};
    b->request("apply_material", {{"image", a["image"]}, {"refs", refs}},
               [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_place_objects") {
    b->request("place", {{"names", a["objects"]}, {"at", a["at"].toString("cursor")}, {"target", a["target"]}},
               [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "blender_match_lighting") {
    b->request("match_lighting", {{"image", a["image"]}}, [done](const QJsonObject &r) { done(r, QString()); });
  }
  else if (n == "generate_image" || n == "generate_video" || n == "generate_3d") {
    const QString kind = n == "generate_image" ? "image" : n == "generate_video" ? "video" : "3d";
    if (key("fal").isEmpty()) {
      done(QJsonObject{{"error", "No fal.ai key configured. Ask the user to add one in Settings → AI Providers."}}, QString());
      return;
    }
    const QString model = profiles_->get(kind == "image" ? "create.image_model"
                                         : kind == "video" ? "create.video_model"
                                                           : "create.model3d_model").toString();
    QVariantMap opts{{"place", a["place"].toString("cursor")}, {"refs", QVariantList()}};
    if (!a["target"].toString().isEmpty()) {
      opts["refs"] = QVariantList{QVariantMap{{"kind", "object"}, {"name", a["target"].toString()}}};
    }
    if (!a["apply_material_to"].toArray().isEmpty()) {
      QVariantList refs;
      for (const auto &o : a["apply_material_to"].toArray()) refs << QVariantMap{{"kind", "object"}, {"name", o.toString()}};
      opts["refs"] = refs;
      opts["applyMaterial"] = true;
    }
    auto start = [this, kind, model, opts, done](const QString &prompt, const QString &image,
                                                  const QMap<QString, QString> &extras) {
      const int id = startJob(kind, model, prompt, QJsonObject(), image, true, extras);
      configureJob(id, opts);
      done(QJsonObject{{"started", true}, {"job", id}, {"model", model}, {"prompt", prompt},
                       {"note", kind == "3d" ? "The model is imported into the scene when it is ready."
                                             : "The result appears in the Library when it is ready."}},
           QString());
    };
    const QString prompt = a["prompt"].toString();
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    if (kind == "video" && a["follow_camera"].toBool()) {
      const QString first = outputsDir() + "/campath-" + stamp + "-first.png";
      const QString last = outputsDir() + "/campath-" + stamp + "-last.png";
      b->request("camera_path", {{"first", first}, {"last", last}, {"width", 1280}},
                 [this, start, prompt, model, first, last, done](const QJsonObject &r) {
                   if (r.contains("error")) {
                     done(r, QString());
                     return;
                   }
                   const QString route = routeFor(model, true);
                   const QString end_key = mediaParam(route, {"end_image", "tail_image", "last_frame",
                                                              "end_frame", "image_tail", "last_image"});
                   QMap<QString, QString> extras;
                   if (!end_key.isEmpty() && r["moving"].toBool()) extras[end_key] = last;
                   start(prompt + " " + r["description"].toString(), first, extras);
                 });
    }
    else if (!a["reference_pass"].toString().isEmpty() && a["reference_pass"].toString() != "beauty") {
      const QString pass = a["reference_pass"].toString();
      const QString path = outputsDir() + "/" + pass + "-" + stamp + ".png";
      b->request("capture_pass", {{"path", path}, {"pass", pass}, {"source", "viewport"}, {"width", 1280},
                                  {"refs", QJsonArray::fromVariantList(opts["refs"].toList())}},
                 [start, prompt, path, done](const QJsonObject &r) {
                   if (r.contains("error")) done(r, QString());
                   else start(prompt, path, {});
                 });
    }
    else if (a["use_viewport"].toBool() || kind != "image") {
      captureViewport([start, prompt](QString path, QString) { start(prompt, path, {}); });
    }
    else {
      start(prompt, QString(), {});
    }
  }
  else {
    done(QJsonObject{{"error", "Unknown tool " + n}}, QString());
  }
}

/** \} */

}  // namespace harness
