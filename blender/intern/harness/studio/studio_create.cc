/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * Create: image, video and 3D generation on fal.ai, with the viewport, the scene camera or a
 * file as reference, and results going straight into the scene or the library.
 */

#include "../harness_bridge.hh"
#include "profiles.hh"
#include "studio.hh"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrl>

#include <QSaveFile>

#include <QSet>

#include <QRegularExpression>

namespace harness {

static const char *kQueue = "https://queue.fal.run";

QString Studio::outputsDir() const
{
  const QString d = Profiles::stateDir() + "/library";
  QDir().mkpath(d);
  return d;
}

static QString model_key(const QString &kind)
{
  return kind == "video" ? "create.video_model" : kind == "3d" ? "create.model3d_model" : "create.image_model";
}

void Studio::setCreateKind(const QString &kind)
{
  if (kind == create_kind_) return;
  create_kind_ = kind;
  if (profiles_) profiles_->set("create.kind", kind);
  Q_EMIT createChanged();
  Q_EMIT createModelsChanged();
}

QString Studio::createModel() const
{
  return profiles_ ? profiles_->get(model_key(create_kind_)).toString() : QString();
}

void Studio::setCreateModel(const QString &endpoint)
{
  if (profiles_) profiles_->set(model_key(create_kind_), endpoint);
  fetchFalSchema(endpoint);
  Q_EMIT createChanged();
}

void Studio::setCreateFile(const QString &f)
{
  const QUrl u(f);
  create_file_ = u.isLocalFile() ? u.toLocalFile() : f;
  if (!create_file_.isEmpty()) create_source_ = "file";
  Q_EMIT createChanged();
}

/** Endpoints of the same family as `endpoint` (text and image routes of one model). */
static QStringList family_endpoints(const QJsonObject &catalog, const QString &endpoint)
{
  const QString fam = catalog[endpoint].toObject()["family"].toString();
  QStringList out;
  for (auto it = catalog.begin(); it != catalog.end(); ++it) {
    if (it->toObject()["family"].toString() == fam) out << it.key();
  }
  if (out.isEmpty()) out << endpoint;
  return out;
}

static QString media_key(const QJsonArray &params)
{
  for (const auto &v : params) {
    const QJsonObject p = v.toObject();
    const QString k = p["key"].toString();
    if (!p["media"].toBool()) continue;
    if (k.contains("image") && !k.startsWith("end_") && !k.startsWith("back_") &&
        !k.startsWith("left_") && !k.startsWith("right_") && !k.startsWith("top_") &&
        !k.startsWith("bottom_") && k != "image_style_url")
    {
      return k;
    }
  }
  return QString();
}

static bool needs_media(const QJsonArray &params)
{
  for (const auto &v : params) {
    if (v.toObject()["media"].toBool() && v.toObject()["required"].toBool()) return true;
  }
  return false;
}

QVariantList Studio::createModels() const
{
  QVariantList out;
  QStringList seen_families;
  for (auto it = catalog_.begin(); it != catalog_.end(); ++it) {
    const QJsonObject e = it->toObject();
    if (e["kind"].toString() != create_kind_ || e["tool"].toBool()) continue;
    const QString fam = e["family"].toString();
    if (seen_families.contains(fam)) continue;
    seen_families << fam;
    /* Show the family once, by its text route when it has one. */
    QString endpoint = it.key();
    for (const QString &ep : family_endpoints(catalog_, it.key())) {
      if (!needs_media(catalog_[ep].toObject()["params"].toArray())) endpoint = ep;
    }
    QString detail = e["provider"].toString();
    if (create_kind_ == "3d") {
      detail = needs_media(catalog_[endpoint].toObject()["params"].toArray()) ? tr("from image") : tr("from text");
    }
    out << QVariantMap{{"id", endpoint}, {"name", e["name"]}, {"detail", detail},
                       {"provider", e["provider"]}, {"curated", true}, {"family", fam},
                       {"icon", modelIcon(endpoint + " " + e["provider"].toString() + " " + e["name"].toString())}};
  }
  auto it = fal_models_.find(create_kind_);
  if (it != fal_models_.end()) {
    for (const auto &v : it->second) {
      const QJsonObject m = v.toObject();
      if (catalog_.contains(m["id"].toString())) continue;
      out << QVariantMap{{"id", m["id"]}, {"name", m["name"]}, {"detail", m["category"]},
                         {"provider", "fal.ai"}, {"curated", false}, {"family", m["id"]},
                         {"icon", modelIcon(m["id"].toString() + " " + m["name"].toString())}};
    }
  }
  return out;
}

QVariantMap Studio::createModelInfo() const
{
  const QString ep = createModel();
  if (ep.isEmpty()) {
    return {};
  }
  if (catalog_.contains(ep)) {
    const QJsonObject e = catalog_[ep].toObject();
    return {{"name", e["name"].toString()},
            {"family", e["family"].toString()},
            {"icon", modelIcon(ep + " " + e["provider"].toString() + " " + e["name"].toString())}};
  }
  auto it = fal_models_.find(create_kind_);
  if (it != fal_models_.end()) {
    for (const auto &v : it->second) {
      const QJsonObject m = v.toObject();
      if (m["id"].toString() == ep) {
        return {{"name", m["name"].toString()}, {"family", ep},
                {"icon", modelIcon(ep + " " + m["name"].toString())}};
      }
    }
  }
  return {{"name", ep.section('/', 1)}, {"family", ep}, {"icon", modelIcon(ep)}};
}

bool Studio::createAcceptsImage() const
{
  for (const QString &ep : family_endpoints(catalog_, createModel())) {
    if (!media_key(catalog_[ep].toObject()["params"].toArray()).isEmpty()) return true;
  }
  return false;
}

QVariantList Studio::createParams() const
{
  QVariantList out;
  const QString ep = createModel();
  /* The settings of the route that will actually run: the image route when a reference
   * image goes with the request, else the text route (their inputs differ). */
  const bool with_image = createAcceptsImage() && create_source_ != "none";
  const QString route = routeFor(ep, with_image);
  const QJsonArray params = catalog_[route].toObject()["params"].toArray().isEmpty()
                                ? catalog_[ep].toObject()["params"].toArray()
                                : catalog_[route].toObject()["params"].toArray();
  const QVariantMap values = create_values_.count(ep) ? create_values_.at(ep) : QVariantMap();
  static const QStringList hidden = {"prompt", "sync_mode", "version", "end_user_id",
                                     "enable_safety_checker", "safety_tolerance", "negative_prompt",
                                     "seed", "enable_prompt_expansion", "output_format", "codec",
                                     "bitrate_mode", "task"};
  for (const auto &v : params) {
    const QJsonObject p = v.toObject();
    const QString k = p["key"].toString(), type = p["type"].toString();
    if (hidden.contains(k) || p["media"].toBool() || type == "array" || type == "object") continue;
    const bool is_enum = p.contains("enum");
    const bool is_bool = type == "boolean";
    const bool is_num = (type == "integer" || type == "number") && p.contains("minimum") && p.contains("maximum");
    if (!is_enum && !is_bool && !is_num) continue;
    QVariant value = values.contains(k) ? values[k] : p["default"].toVariant();
    QVariantMap m{{"key", k}, {"title", p["title"].toString()},
                  {"kind", is_bool ? "toggle" : is_num ? "slider" : "choice"},
                  {"integer", type == "integer"}};
    if (is_enum) {
      QStringList opts;
      for (const auto &e : p["enum"].toArray()) opts << e.toVariant().toString();
      m["options"] = opts;
      if (!value.isValid()) value = opts.value(0);
    }
    if (is_num) {
      m["min"] = p["minimum"].toDouble();
      m["max"] = p["maximum"].toDouble();
      if (!value.isValid()) value = p["minimum"].toDouble();
    }
    if (is_bool && !value.isValid()) value = false;
    m["value"] = value;
    out << m;
  }
  return out;
}

void Studio::setCreateParam(const QString &key, const QVariant &value)
{
  create_values_[createModel()][key] = value;
  Q_EMIT createChanged();
}

QVariantList Studio::jobs() const
{
  QVariantList out;
  for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
    if (!it->preset_id.isEmpty()) continue;
    out << QVariantMap{{"id", it->id}, {"kind", it->kind}, {"title", it->title},
                       {"prompt", it->prompt}, {"status", it->status}, {"message", it->message},
                       {"progress", it->progress}, {"file", it->file},
                       {"url", it->file.isEmpty() ? QString() : QUrl::fromLocalFile(it->file).toString()},
                       {"time", QDateTime::fromMSecsSinceEpoch(it->created).toString("HH:mm")}};
  }
  return out;
}

bool Studio::creating() const
{
  for (const auto &j : jobs_) {
    if (!j.preset_id.isEmpty()) continue;
    if (j.status == "queued" || j.status == "running" || j.status == "uploading") return true;
  }
  return false;
}

Studio::Job *Studio::job(int id)
{
  for (auto &j : jobs_) {
    if (j.id == id) return &j;
  }
  return nullptr;
}

static const QStringList kEndFrameKeys = {"end_image", "tail_image", "last_frame", "end_frame",
                                           "image_tail", "last_image"};

bool Studio::createTakesEndFrame() const
{
  return create_kind_ == "video" && !mediaParam(routeFor(createModel(), true), kEndFrameKeys).isEmpty();
}

void Studio::setCreateOption(const QString &key, const QVariant &value)
{
  create_options_[key] = value;
  Q_EMIT createChanged();
}

void Studio::pickRegion()
{
  create_options_["region"] = true;
  Q_EMIT createChanged();
  Bridge::instance()->request("pick_region", {}, [](const QJsonObject &) {});
}

/* Scene link options travel with the job: where a 3D result goes, which objects an image
 * result dresses. */
void Studio::configureJob(int id, const QVariantMap &opts)
{
  Job *j = job(id);
  if (!j) return;
  const QVariantList refs = opts["refs"].toList();
  j->place = opts["place"].toString().isEmpty() ? QStringLiteral("cursor") : opts["place"].toString();
  for (const QVariant &r : refs) {
    const QVariantMap m = r.toMap();
    if (m["kind"] != "material" && m["kind"] != "collection" && j->place_target.isEmpty()) {
      j->place_target = m["name"].toString();
    }
  }
  if (j->place == "object" && j->place_target.isEmpty()) j->place = "cursor";
  if (j->kind == "image" && opts["applyMaterial"].toBool()) {
    j->material_refs = QJsonArray::fromVariantList(refs);
  }
}

void Studio::createFromScene(const QString &ep, const QString &prompt, const QJsonObject &input,
                             bool auto_import)
{
  const QVariantMap opts = create_options_;
  const QString kind = create_kind_;
  const QJsonArray refs = QJsonArray::fromVariantList(opts["refs"].toList());
  auto launch = [this, kind, ep, input, auto_import, opts](const QString &text, const QString &image,
                                                          const QMap<QString, QString> &extras,
                                                          const QString &route_override) {
    const int id = startJob(kind, route_override.isEmpty() ? ep : route_override, text, input,
                            image, auto_import, extras);
    configureJob(id, opts);
  };
  const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
  /* Regional edit: the viewport image with a mask of the picked area. */
  if (kind == "image" && opts["region"].toBool()) {
    QString route = routeFor(ep, true);
    QString mask_key = mediaParam(route, {"mask"});
    if (mask_key.isEmpty()) {
      /* The chosen model can't fill a mask: use an inpainting model from the catalogue. */
      for (auto it = catalog_.begin(); it != catalog_.end() && mask_key.isEmpty(); ++it) {
        if (it->toObject()["kind"].toString() != "image") continue;
        const QString k = mediaParam(it.key(), {"mask"});
        if (!k.isEmpty() && !media_key(it->toObject()["params"].toArray()).isEmpty()) {
          route = it.key();
          mask_key = k;
        }
      }
    }
    if (mask_key.isEmpty()) {
      Q_EMIT notify("error", tr("No model in the list can repaint an area (it needs a mask input)."));
      return;
    }
    const QString img = outputsDir() + "/region-" + stamp + ".png";
    const QString mask = outputsDir() + "/region-" + stamp + "-mask.png";
    Bridge::instance()->request("region_capture", {{"image", img}, {"mask", mask}, {"width", 1280}},
                                [this, launch, prompt, img, mask, mask_key, route](const QJsonObject &r) {
                                  if (r.contains("error")) {
                                    Q_EMIT notify("error", r["error"].toString());
                                    return;
                                  }
                                  launch(prompt, img, {{mask_key, mask}}, route);
                                });
    return;
  }
  /* Video along the 3D camera path: first frame as the start image, last frame as the end
   * image when the model takes one, and the move in words. */
  if (kind == "video" && opts["followCamera"].toBool()) {
    const QString first = outputsDir() + "/campath-" + stamp + "-first.png";
    const QString last = outputsDir() + "/campath-" + stamp + "-last.png";
    Bridge::instance()->request(
        "camera_path", {{"first", first}, {"last", last}, {"width", 1280}},
        [this, launch, prompt, ep, first, last](const QJsonObject &r) {
          if (r.contains("error")) {
            Q_EMIT notify("error", r["error"].toString());
            return;
          }
          QString text = prompt.trimmed();
          const QString desc = r["description"].toString();
          text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + desc +
                  (r["moving"].toBool() ? tr(" Follow this camera motion exactly; keep the framing of the start image.")
                                        : QString());
          const QString route = routeFor(ep, true);
          QMap<QString, QString> extras;
          const QString end_key = mediaParam(route, {"end_image", "tail_image", "last_frame", "end_frame",
                                                     "image_tail", "last_image"});
          if (!end_key.isEmpty() && r["moving"].toBool()) extras[end_key] = last;
          launch(text, first, extras, QString());
        });
    return;
  }
  /* A reference pass of the scene: depth, normal, edges or object mask. */
  const QString pass = opts["pass"].toString();
  const QString path = outputsDir() + "/" + pass + "-" + stamp + ".png";
  Bridge::instance()->request("capture_pass",
                              {{"path", path}, {"pass", pass}, {"source", create_source_},
                               {"refs", refs}, {"width", 1280}},
                              [this, launch, prompt, path](const QJsonObject &r) {
                                if (r.contains("error")) {
                                  Q_EMIT notify("error", r["error"].toString().section('\n', -2));
                                  return;
                                }
                                launch(prompt, path, {}, QString());
                              });
}

void Studio::create(const QString &prompt)
{
  const QString ep = createModel();
  if (ep.isEmpty()) return;
  QJsonObject input;
  const QVariantMap values = create_values_.count(ep) ? create_values_.at(ep) : QVariantMap();
  for (auto it = values.begin(); it != values.end(); ++it) input[it.key()] = QJsonValue::fromVariant(it.value());
  const bool auto_import = profiles_ && profiles_->get("create.auto_import").toBool();
  const QVariantMap opts = create_options_;
  const bool scene_source = create_source_ == "viewport" || create_source_ == "camera";
  if ((create_kind_ == "image" && opts["region"].toBool()) ||
      (create_kind_ == "video" && opts["followCamera"].toBool()) ||
      (createAcceptsImage() && scene_source && opts["pass"].toString() != "beauty" &&
       !opts["pass"].toString().isEmpty()))
  {
    createFromScene(ep, prompt, input, auto_import);
    return;
  }
  const bool wants_image = createAcceptsImage() && create_source_ != "none";
  bool must_have_image = true;
  for (const QString &fe : family_endpoints(catalog_, ep)) {
    must_have_image &= needs_media(catalog_[fe].toObject()["params"].toArray());
  }
  if (!wants_image) {
    if (must_have_image) {
      Q_EMIT notify("error", tr("This model needs a reference image: choose Viewport, Camera or a file."));
      return;
    }
    configureJob(startJob(create_kind_, ep, prompt, input, QString(), auto_import), opts);
    return;
  }
  if (create_source_ == "file") {
    if (create_file_.isEmpty() || !QFile::exists(create_file_)) {
      Q_EMIT notify("error", tr("Choose a reference image first."));
      return;
    }
    /* First and last frame: the end frame goes to the model's end-image input. */
    QMap<QString, QString> extras;
    const QUrl end_url(opts["endFile"].toString());
    const QString end_file = end_url.isLocalFile() ? end_url.toLocalFile() : opts["endFile"].toString();
    if (create_kind_ == "video" && !end_file.isEmpty() && QFile::exists(end_file)) {
      const QString end_key = mediaParam(routeFor(ep, true), kEndFrameKeys);
      if (!end_key.isEmpty()) extras[end_key] = end_file;
    }
    configureJob(startJob(create_kind_, ep, prompt, input, create_file_, auto_import, extras), opts);
    return;
  }
  if (create_source_ == "camera") {
    const QString path = outputsDir() + QString("/camera-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    Bridge::instance()->request("render_still", {{"path", path}},
                                [this, ep, prompt, input, path, auto_import, opts](const QJsonObject &r) {
                                  if (r.contains("error")) {
                                    Q_EMIT notify("error", r["error"].toString());
                                    return;
                                  }
                                  configureJob(startJob(create_kind_, ep, prompt, input, path, auto_import), opts);
                                });
    return;
  }
  captureViewport([this, ep, prompt, input, auto_import, opts](QString path, QString err) {
    if (path.isEmpty()) {
      Q_EMIT notify("error", err);
      return;
    }
    configureJob(startJob(create_kind_, ep, prompt, input, path, auto_import), opts);
  });
}

QString Studio::routeFor(const QString &endpoint, bool has_image) const
{
  /* An image route of the family when there is an image, else the text route. */
  QString route = endpoint;
  for (const QString &ep : family_endpoints(catalog_, endpoint)) {
    const QJsonArray params = catalog_[ep].toObject()["params"].toArray();
    if (has_image && !media_key(params).isEmpty()) {
      return ep;
    }
    if (!has_image && !needs_media(params)) route = ep;
  }
  return route;
}

QString Studio::mediaParam(const QString &route, const QStringList &needles) const
{
  for (const auto &v : catalog_[route].toObject()["params"].toArray()) {
    const QJsonObject p = v.toObject();
    if (!p["media"].toBool()) continue;
    const QString k = p["key"].toString();
    for (const QString &n : needles) {
      if (k.contains(n)) return k;
    }
  }
  return QString();
}

void Studio::uploadAll(QStringList keys, QMap<QString, QString> media, QJsonObject body, int id,
                       std::function<void(QJsonObject)> done)
{
  if (keys.isEmpty()) {
    done(body);
    return;
  }
  const QString k = keys.takeFirst();
  bool is_array = false;
  if (Job *j = job(id)) {
    for (const auto &v : catalog_[j->endpoint].toObject()["params"].toArray()) {
      if (v.toObject()["key"].toString() == k) is_array = v.toObject()["type"].toString() == "array";
    }
  }
  uploadToFal(media[k], [this, keys, media, body, id, done, k, is_array](QString url, QString err) mutable {
    if (!err.isEmpty()) {
      finishJob(id, false, err);
      return;
    }
    body[k] = is_array ? QJsonValue(QJsonArray{url}) : QJsonValue(url);
    uploadAll(keys, media, body, id, done);
  });
}

int Studio::startJob(const QString &kind, const QString &endpoint, const QString &prompt,
                     const QJsonObject &input, const QString &image_path, bool auto_import,
                     const QMap<QString, QString> &extra_media)
{
  Job j;
  j.id = next_job_++;
  j.kind = kind;
  j.prompt = prompt;
  j.status = "queued";
  j.created = QDateTime::currentMSecsSinceEpoch();
  j.auto_import = auto_import;
  /* The route: an image route of the family when there is an image, else the text route. */
  QString route = endpoint;
  const QStringList fam = family_endpoints(catalog_, endpoint);
  for (const QString &ep : fam) {
    const QJsonArray params = catalog_[ep].toObject()["params"].toArray();
    const QString mk = media_key(params);
    if (!image_path.isEmpty() && !mk.isEmpty()) {
      route = ep;
      break;
    }
    if (image_path.isEmpty() && !needs_media(params)) route = ep;
  }
  j.endpoint = route;
  j.title = catalog_[route].toObject()["name"].toString(route.section('/', -2));
  jobs_.push_back(j);
  Q_EMIT jobsChanged();

  if (key("fal").isEmpty()) {
    finishJob(j.id, false, tr("Add your fal.ai key in Settings → AI Providers."));
    return j.id;
  }
  QJsonObject body = input;
  const QJsonArray params = catalog_[route].toObject()["params"].toArray();
  if (!params.isEmpty()) {
    /* Only send what this route accepts (a setting of the text route would be rejected or
     * ignored by the image route). */
    QSet<QString> accepted;
    for (const auto &v : params) accepted.insert(v.toObject()["key"].toString());
    for (const QString &k : body.keys()) {
      if (!accepted.contains(k)) body.remove(k);
    }
  }
  bool takes_prompt = params.isEmpty();
  for (const auto &v : params) takes_prompt |= v.toObject()["key"].toString() == "prompt";
  if (!prompt.trimmed().isEmpty() && takes_prompt) body["prompt"] = prompt.trimmed();
  const QString mk = media_key(params);
  QMap<QString, QString> media = extra_media;
  if (!image_path.isEmpty() && !mk.isEmpty()) {
    media[mk] = image_path;
  }
  if (media.isEmpty()) {
    submitJob(j.id, body);
    return j.id;
  }
  Job *jj = job(j.id);
  jj->status = "uploading";
  jj->message = tr("Uploading reference");
  Q_EMIT jobsChanged();
  const int id = j.id;
  uploadAll(media.keys(), media, body, id, [this, id](QJsonObject b) { submitJob(id, b); });
  return j.id;
}

void Studio::uploadToFal(const QString &path, std::function<void(QString, QString)> done)
{
  QNetworkRequest tr_req(QUrl("https://rest.fal.ai/storage/auth/token?storage_type=fal-cdn-v3"));
  tr_req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
  tr_req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  tr_req.setTransferTimeout(30000);
  QNetworkReply *rep = net_->post(tr_req, "{}");
  connect(rep, &QNetworkReply::finished, this, [this, rep, path, done]() {
    rep->deleteLater();
    if (rep->error() != QNetworkReply::NoError) {
      done(QString(), tr("fal.ai upload: %1").arg(rep->errorString()));
      return;
    }
    const QJsonObject t = QJsonDocument::fromJson(rep->readAll()).object();
    QString base = t["base_url"].toString("https://v3.fal.media");
    while (base.endsWith('/')) base.chop(1);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      done(QString(), tr("Cannot read %1").arg(path));
      return;
    }
    QNetworkRequest up(QUrl(base + "/files/upload"));
    up.setRawHeader("Authorization", (t["token_type"].toString() + " " + t["token"].toString()).toUtf8());
    up.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(path).name());
    up.setRawHeader("X-Fal-File-Name", QFileInfo(path).fileName().toUtf8());
    up.setTransferTimeout(300000);
    QNetworkReply *r2 = net_->post(up, f.readAll());
    connect(r2, &QNetworkReply::finished, this, [r2, done]() {
      r2->deleteLater();
      const QJsonObject o = QJsonDocument::fromJson(r2->readAll()).object();
      if (r2->error() != QNetworkReply::NoError || o["access_url"].toString().isEmpty()) {
        done(QString(), QObject::tr("fal.ai upload: %1").arg(r2->errorString()));
        return;
      }
      done(o["access_url"].toString(), QString());
    });
  });
}

void Studio::submitJob(int id, const QJsonObject &body)
{
  Job *j = job(id);
  if (!j) return;
  QNetworkRequest req(QUrl(QString(kQueue) + "/" + j->endpoint));
  req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setTransferTimeout(60000);
  QNetworkReply *rep = net_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(rep, &QNetworkReply::finished, this, [this, rep, id]() {
    rep->deleteLater();
    const QByteArray data = rep->readAll();
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    Job *j = job(id);
    if (!j) return;
    if (rep->error() != QNetworkReply::NoError || !o.contains("request_id")) {
      QString msg = o["detail"].isString() ? o["detail"].toString()
                                           : QString::fromUtf8(QJsonDocument(o["detail"].toArray()).toJson(QJsonDocument::Compact)).left(300);
      finishJob(id, false, msg.isEmpty() || msg == "[]" ? rep->errorString() : msg);
      return;
    }
    /* The queue answers with the URLs to use; never rebuild them from the endpoint. */
    j->status_url = o["status_url"].toString();
    j->response_url = o["response_url"].toString();
    j->cancel_url = o["cancel_url"].toString();
    j->status = "running";
    j->message = tr("In queue");
    j->progress = 0.05;
    Q_EMIT jobsChanged();
    poll_timer_.start();
  });
}

void Studio::pollJobs()
{
  bool any = false;
  for (auto &jr : jobs_) {
    if (jr.status != "running" || jr.status_url.isEmpty()) continue;
    any = true;
    const int id = jr.id;
    QNetworkRequest req(QUrl(jr.status_url));
    req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
    QNetworkReply *rep = net_->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, id]() {
      rep->deleteLater();
      Job *j = job(id);
      if (!j || j->status != "running" || rep->error() != QNetworkReply::NoError) return;
      const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
      const QString st = o["status"].toString();
      if (st == "IN_QUEUE") {
        j->message = tr("In queue · position %1").arg(o["queue_position"].toInt() + 1);
      }
      else if (st == "IN_PROGRESS") {
        j->message = tr("Generating");
        j->progress = std::min(0.92, j->progress + 0.025);
      }
      else if (st == "COMPLETED") {
        j->status = "downloading";
        j->message = tr("Downloading");
        j->progress = 0.96;
        fetchJobResult(id);
      }
      Q_EMIT jobsChanged();
    });
  }
  if (!any) poll_timer_.stop();
}

static void collect_urls(const QJsonValue &v, QStringList &out)
{
  if (v.isObject()) {
    const QJsonObject o = v.toObject();
    if (o["url"].isString()) out << o["url"].toString();
    for (auto it = o.begin(); it != o.end(); ++it) {
      if (it.key() != "url") collect_urls(it.value(), out);
    }
  }
  else if (v.isArray()) {
    for (const auto &x : v.toArray()) collect_urls(x, out);
  }
}

void Studio::fetchJobResult(int id)
{
  Job *j = job(id);
  if (!j) return;
  QNetworkRequest req(QUrl(j->response_url));
  req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
  QNetworkReply *rep = net_->get(req);
  connect(rep, &QNetworkReply::finished, this, [this, rep, id]() {
    rep->deleteLater();
    Job *j = job(id);
    if (!j) return;
    const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
    if (rep->error() != QNetworkReply::NoError) {
      finishJob(id, false, o["detail"].toVariant().toString().left(300));
      return;
    }
    QStringList urls;
    collect_urls(o, urls);
    auto pick = [&](const QStringList &exts) -> QString {
      for (const QString &e : exts) {
        for (const QString &u : urls) {
          if (QUrl(u).path().endsWith(e, Qt::CaseInsensitive)) return u;
        }
      }
      return QString();
    };
    QString url;
    if (j->kind == "3d") url = pick({".glb", ".gltf", ".fbx", ".obj"});
    else if (j->kind == "video") url = pick({".mp4", ".webm", ".mov"});
    if (url.isEmpty()) url = pick({".png", ".jpg", ".jpeg", ".webp"});
    if (url.isEmpty() && !urls.isEmpty()) url = urls.first();
    if (url.isEmpty()) {
      finishJob(id, false, tr("The result has no file."));
      return;
    }
    QNetworkReply *dl = net_->get(QNetworkRequest(QUrl(url)));
    connect(dl, &QNetworkReply::finished, this, [this, dl, id, url]() {
      dl->deleteLater();
      if (dl->error() != QNetworkReply::NoError) {
        finishJob(id, false, tr("Download failed: %1").arg(dl->errorString()));
        return;
      }
      QString name = QFileInfo(QUrl(url).path()).fileName();
      if (name.isEmpty()) name = "result.bin";
      const QString path = outputsDir() + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-" + name;
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly)) {
        finishJob(id, false, tr("Cannot write %1").arg(path));
        return;
      }
      f.write(dl->readAll());
      f.close();
      if (Job *j = job(id)) j->file = path;
      finishJob(id, true, QString());
    });
  });
}

static QString preset_dir()
{
  const QString d = Profiles::stateDir() + "/presets";
  QDir().mkpath(d);
  return d;
}

QString Studio::presetPreview(const QString &id) const
{
  for (const QString &ext : {"jpg", "jpeg", "png", "webp"}) {
    const QString f = preset_dir() + "/" + id + "." + ext;
    if (QFile::exists(f)) return QUrl::fromLocalFile(f).toString();
  }
  return QString();
}

void Studio::generatePresetPreviews(const QVariantList &presets)
{
  if (key("fal").isEmpty()) {
    Q_EMIT notify("error", tr("Add your fal.ai key in Settings → AI Providers to make previews."));
    return;
  }
  /* The cheapest fast text-to-image model: FLUX schnell (about $0.003 per picture). */
  const QString endpoint = QStringLiteral("fal-ai/flux/schnell");
  int n = 0;
  for (const QVariant &v : presets) {
    const QVariantMap p = v.toMap();
    const QString cat = p["cat"].toString();
    const QString subject = (cat == "product" || cat == "productimg") ? "a premium perfume bottle on a plinth"
                          : cat == "3d" ? "a clean 3D model on a turntable, studio render"
                          : "a stylised character on a stage";
    const QString prompt = p["prompt"].toString() + ", " + subject +
                           ", cinematic still frame, dark studio, warm key light and cool rim light, high detail";
    QJsonObject input{{"image_size", "landscape_16_9"}, {"num_inference_steps", 4}};
    const int jid = startJob("image", endpoint, prompt, input, QString(), false);
    if (Job *j = job(jid)) j->preset_id = p["id"].toString();
    n++;
  }
  Q_EMIT notify("info", tr("Making %1 previews with fal.ai").arg(n));
}

void Studio::finishJob(int id, bool ok, const QString &message)
{
  Job *j = job(id);
  if (!j) return;
  if (!j->preset_id.isEmpty()) {
    /* A preset preview: into the preview cache, out of the list. */
    if (ok && !j->file.isEmpty()) {
      const QString ext = QFileInfo(j->file).suffix().toLower();
      for (const QString &old : {"jpg", "jpeg", "png", "webp"}) QFile::remove(preset_dir() + "/" + j->preset_id + "." + old);
      QFile::rename(j->file, preset_dir() + "/" + j->preset_id + "." + (ext.isEmpty() ? "png" : ext));
      presets_revision_++;
      Q_EMIT presetsChanged();
    }
    for (size_t i = 0; i < jobs_.size(); i++) {
      if (jobs_[i].id == id) {
        jobs_.erase(jobs_.begin() + long(i));
        break;
      }
    }
    Q_EMIT jobsChanged();
    return;
  }
  j->status = ok ? "done" : "failed";
  j->message = message;
  j->progress = 1.0;
  Q_EMIT jobsChanged();
  if (!ok) {
    Q_EMIT notify("error", tr("%1 failed: %2").arg(j->title, message));
    return;
  }
  Q_EMIT notify("done", tr("%1 is ready").arg(j->title));
  if (j->auto_import && j->kind == "3d") {
    useJob(id, "object");
  }
  if (j->kind == "image" && !j->material_refs.isEmpty()) {
    applyJobMaterial(id, j->material_refs.toVariantList());
  }
  if (busy_ || !messages_.isEmpty()) {
    appendMessage({{"role", "notice"}, {"kind", "done"},
                   {"text", tr("%1 finished%2.").arg(j->title, j->kind == "3d" && j->auto_import ? tr(" and was imported") : QString())},
                   {"images", (j->kind == "image") ? QStringList{QUrl::fromLocalFile(j->file).toString()} : QStringList()}});
  }
}

void Studio::cancelJob(int id)
{
  Job *j = job(id);
  if (!j) return;
  if (!j->cancel_url.isEmpty()) {
    QNetworkRequest req(QUrl(j->cancel_url));
    req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
    QNetworkReply *rep = net_->put(req, QByteArray());
    connect(rep, &QNetworkReply::finished, rep, &QObject::deleteLater);
  }
  j->status = "failed";
  j->message = tr("Cancelled");
  Q_EMIT jobsChanged();
}

void Studio::useJob(int id, const QString &as)
{
  Job *j = job(id);
  if (!j || j->file.isEmpty()) return;
  QJsonObject payload{{"path", j->file}, {"as", as}};
  if (j->kind == "3d" && as == "object") {
    payload["place"] = j->place;
    payload["target"] = j->place_target;
  }
  Bridge::instance()->request("import_file", payload,
                              [this](const QJsonObject &r) {
                                if (r.contains("error")) Q_EMIT notify("error", r["error"].toString());
                                else Q_EMIT notify("done", tr("Added to the scene"));
                              });
}

void Studio::removeJob(int id)
{
  for (size_t i = 0; i < jobs_.size(); i++) {
    if (jobs_[i].id == id && jobs_[i].status != "running" && jobs_[i].status != "uploading") {
      if (!jobs_[i].file.isEmpty()) removed_files_ << QFileInfo(jobs_[i].file).fileName();
      jobs_.erase(jobs_.begin() + long(i));
      Q_EMIT jobsChanged();
      return;
    }
  }
}

void Studio::revealJob(int id)
{
  Job *j = job(id);
  if (j && !j->file.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(j->file).absolutePath()));
  }
}

void Studio::applyJobMaterial(int id, const QVariantList &refs)
{
  Job *j = job(id);
  if (!j || j->file.isEmpty()) return;
  Bridge::instance()->request("apply_material",
                              {{"image", j->file}, {"refs", QJsonArray::fromVariantList(refs)}},
                              [this](const QJsonObject &r) {
                                if (r.contains("error")) Q_EMIT notify("error", r["error"].toString());
                                else Q_EMIT notify("done", tr("Material %1 applied to %2")
                                                               .arg(r["material"].toString(),
                                                                    r["objects"].toVariant().toStringList().join(", ")));
                              });
}

void Studio::matchJobLighting(int id)
{
  Job *j = job(id);
  if (!j || j->file.isEmpty()) return;
  Bridge::instance()->request("match_lighting", {{"image", j->file}}, [this](const QJsonObject &r) {
    if (r.contains("error")) Q_EMIT notify("error", r["error"].toString());
    else Q_EMIT notify("done", tr("Scene lighting matched to the image"));
  });
}

void Studio::placeJob(int id, const QString &at, const QString &target)
{
  Job *j = job(id);
  if (!j) return;
  j->place = at;
  j->place_target = target;
  useJob(id, "object");
}

void Studio::saveJobs() const
{
  QJsonArray arr;
  for (const Job &j : jobs_) {
    if (!j.preset_id.isEmpty()) continue;
    arr << QJsonObject{{"id", j.id}, {"kind", j.kind}, {"endpoint", j.endpoint}, {"title", j.title},
                       {"prompt", j.prompt}, {"status", j.status}, {"message", j.message},
                       {"file", j.file}, {"progress", j.progress}, {"created", double(j.created)},
                       {"status_url", j.status_url}, {"response_url", j.response_url},
                       {"cancel_url", j.cancel_url}, {"auto_import", j.auto_import},
                       {"place", j.place}, {"place_target", j.place_target},
                       {"material_refs", j.material_refs}};
  }
  QSaveFile f(outputsDir() + "/library.json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"jobs", arr},
                                      {"removed", QJsonArray::fromStringList(removed_files_)}})
                .toJson(QJsonDocument::Compact));
    f.commit();
  }
}

void Studio::loadJobs()
{
  QFile f(outputsDir() + "/library.json");
  QSet<QString> known;
  bool resume = false;
  if (f.open(QIODevice::ReadOnly)) {
    const QJsonObject db = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto &r : db["removed"].toArray()) {
      removed_files_ << r.toString();
      known.insert(r.toString());
    }
    for (const auto &v : db["jobs"].toArray()) {
      const QJsonObject o = v.toObject();
      Job j;
      j.id = o["id"].toInt();
      j.kind = o["kind"].toString();
      j.endpoint = o["endpoint"].toString();
      j.title = o["title"].toString();
      j.prompt = o["prompt"].toString();
      j.status = o["status"].toString();
      j.message = o["message"].toString();
      j.file = o["file"].toString();
      j.progress = o["progress"].toDouble();
      j.created = qint64(o["created"].toDouble());
      j.status_url = o["status_url"].toString();
      j.response_url = o["response_url"].toString();
      j.cancel_url = o["cancel_url"].toString();
      j.auto_import = false; /* never import again behind the person's back */
      j.place = o["place"].toString("cursor");
      j.place_target = o["place_target"].toString();
      j.material_refs = o["material_refs"].toArray();
      if (j.status == "done" && !j.file.isEmpty() && !QFile::exists(j.file)) {
        continue; /* the file was deleted outside the app */
      }
      if (j.status != "done" && j.status != "failed") {
        if (!j.status_url.isEmpty()) {
          /* fal keeps queued requests: carry on polling where the last session stopped. */
          j.status = "running";
          j.message = tr("Resuming");
          resume = true;
        }
        else {
          j.status = "failed";
          j.message = tr("Stopped when the app closed, before it was sent");
        }
      }
      next_job_ = std::max(next_job_, j.id + 1);
      if (!j.file.isEmpty()) known.insert(QFileInfo(j.file).fileName());
      jobs_.push_back(j);
    }
  }
  /* Results saved before the library kept records: bring them in too. */
  static const QRegularExpression result_name("^\\d{8}-\\d{6}-");
  const QFileInfoList files = QDir(outputsDir()).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
  for (const QFileInfo &fi : files) {
    if (known.contains(fi.fileName()) || !result_name.match(fi.fileName()).hasMatch()) continue;
    const QString ext = fi.suffix().toLower();
    QString kind;
    if (QStringList{"png", "jpg", "jpeg", "webp"}.contains(ext)) kind = "image";
    else if (QStringList{"mp4", "webm", "mov"}.contains(ext)) kind = "video";
    else if (QStringList{"glb", "gltf", "fbx", "obj"}.contains(ext)) kind = "3d";
    if (kind.isEmpty()) continue;
    Job j;
    j.id = next_job_++;
    j.kind = kind;
    j.title = fi.completeBaseName().mid(16);
    j.status = "done";
    j.progress = 1.0;
    j.file = fi.filePath();
    j.created = fi.lastModified().toMSecsSinceEpoch();
    jobs_.push_back(j);
  }
  std::stable_sort(jobs_.begin(), jobs_.end(), [](const Job &a, const Job &b) { return a.created < b.created; });
  if (resume) poll_timer_.start();
  Q_EMIT jobsChanged();
}

}  // namespace harness
