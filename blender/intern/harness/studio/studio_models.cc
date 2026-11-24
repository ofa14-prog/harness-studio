/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * Provider registry and live model catalogs.
 */

#include "profiles.hh"
#include "studio.hh"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QUrlQuery>

namespace harness {

struct ProviderInfo {
  const char *id, *name, *icon, *kind, *docs;
};

/* `icon` is a Simple Icons slug (brand marks) shipped in resources/icons/brands. */
static const ProviderInfo kProviders[] = {
    {"anthropic", "Anthropic", "anthropic", "chat", "https://docs.anthropic.com/en/api/messages"},
    {"openai", "OpenAI", "openai", "chat", "https://platform.openai.com/docs/api-reference/chat"},
    {"google", "Google Gemini", "googlegemini", "chat",
     "https://ai.google.dev/gemini-api/docs/openai"},
    {"openrouter", "OpenRouter", "openrouter", "chat", "https://openrouter.ai/docs/api-reference"},
    {"ollama", "Ollama (local)", "ollama", "chat", "https://github.com/ollama/ollama/blob/main/docs/openai.md"},
    {"fal", "fal.ai", "fal", "media", "https://docs.fal.ai/model-apis/model-endpoints/queue"},
};

Studio *Studio::instance()
{
  static Studio *s = new Studio();
  return s;
}

QString Studio::key(const QString &provider) const
{
  return profiles_ ? profiles_->apiKey(provider) : QString();
}

QString Studio::providerIcon(const QString &provider) const
{
  for (const auto &p : kProviders) {
    if (provider == QLatin1String(p.id)) return QString::fromLatin1(p.icon);
  }
  return QString();
}

QString Studio::modelIcon(const QString &model, const QString &provider) const
{
  /* Checked in order: the first needle found in the model id names the maker. Only marks
   * that ship in resources/icons/brands are listed. */
  static const struct {
    const char *needle, *icon;
  } kMakers[] = {
      {"anthropic", "anthropic"}, {"claude", "anthropic"},
      {"openai", "openai"}, {"gpt", "openai"}, {"sora", "openai"}, {"dall-e", "openai"},
      {"chatgpt", "openai"}, {"/o1", "openai"}, {"/o3", "openai"}, {"/o4", "openai"},
      {"nano-banana", "googlegemini"}, {"gemini", "googlegemini"}, {"imagen", "google"},
      {"veo", "google"}, {"gemma", "google"}, {"google", "google"},
      {"meta-llama", "meta"}, {"llama", "meta"},
      {"mistral", "mistralai"}, {"codestral", "mistralai"}, {"pixtral", "mistralai"},
      {"magistral", "mistralai"}, {"devstral", "mistralai"},
      {"deepseek", "deepseek"},
      {"x-ai", "x"}, {"grok", "x"},
      {"qwen", "qwen"}, {"/wan", "alibabacloud"}, {"wan-", "alibabacloud"}, {"alibaba", "alibabacloud"},
      {"kling", "kuaishou"}, {"kuaishou", "kuaishou"},
      {"hailuo", "minimax"}, {"minimax", "minimax"},
      {"flux", "flux"}, {"black-forest", "flux"},
      {"moonshot", "moonshotai"}, {"kimi", "moonshotai"},
      {"nvidia", "nvidia"}, {"nemotron", "nvidia"},
      {"seedance", "bytedance"}, {"seedream", "bytedance"}, {"bytedance", "bytedance"},
      {"hunyuan", "tencenthy"}, {"tencent", "tencenthy"},
      {"perplexity", "perplexity"}, {"sonar", "perplexity"},
      {"elevenlabs", "elevenlabs"},
      {"ernie", "baidu"}, {"baidu", "baidu"},
      {"mimo", "xiaomi"}, {"xiaomi", "xiaomi"},
      {"huggingface", "huggingface"},
  };
  const QString id = model.toLower();
  for (const auto &m : kMakers) {
    if (id.contains(QLatin1String(m.needle))) {
      return QString::fromLatin1(m.icon);
    }
  }
  return providerIcon(provider);
}

QString Studio::providerName(const QString &provider) const
{
  for (const auto &p : kProviders) {
    if (provider == QLatin1String(p.id)) return QString::fromLatin1(p.name);
  }
  return provider;
}

QVariantList Studio::providers() const
{
  QVariantList out;
  for (const auto &p : kProviders) {
    const QString id = QString::fromLatin1(p.id);
    const bool local = id == QLatin1String("ollama");
    out << QVariantMap{
        {"id", id},
        {"name", QString::fromLatin1(p.name)},
        {"icon", QString::fromLatin1(p.icon)},
        {"kind", QString::fromLatin1(p.kind)},
        {"docs", QString::fromLatin1(p.docs)},
        {"needsKey", !local},
        {"hasKey", local || (profiles_ && profiles_->hasApiKey(id))},
        {"maskedKey", profiles_ ? profiles_->maskedApiKey(id) : QString()},
        {"keySource", profiles_ ? profiles_->apiKeySource(id) : QString()},
        {"env", Profiles::envNameFor(id)},
        {"enabled", profiles_ ? profiles_->get("providers." + id + ".enabled").toBool() : true},
        {"modelCount", int(models_.count(id) ? models_.at(id).size() : 0)},
        {"error", model_errors_.count(id) ? model_errors_.at(id) : QString()},
    };
  }
  return out;
}

QString Studio::chatBase(const QString &provider) const
{
  if (provider == "anthropic") return "https://api.anthropic.com/v1";
  if (provider == "openai") return "https://api.openai.com/v1";
  if (provider == "google") return "https://generativelanguage.googleapis.com/v1beta/openai";
  if (provider == "openrouter") return "https://openrouter.ai/api/v1";
  if (provider == "ollama") {
    QString url = profiles_ ? profiles_->get("providers.ollama.url").toString() : QString();
    if (url.isEmpty()) url = "http://127.0.0.1:11434";
    while (url.endsWith('/')) url.chop(1);
    return url + "/v1";
  }
  return QString();
}

static QString cache_dir()
{
  const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                    QStringLiteral("/HarnessStudio");
  QDir().mkpath(d);
  return d;
}

void Studio::loadModelCache()
{
  for (const auto &p : kProviders) {
    QFile f(cache_dir() + "/models-" + p.id + ".json");
    if (f.open(QIODevice::ReadOnly)) {
      models_[QString::fromLatin1(p.id)] = QJsonDocument::fromJson(f.readAll()).object()["models"].toArray();
    }
  }
}

void Studio::saveModelCache(const QString &provider)
{
  QFile f(cache_dir() + "/models-" + provider + ".json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{"fetched", QDateTime::currentDateTime().toString(Qt::ISODate)},
                                      {"models", models_[provider]}})
                .toJson(QJsonDocument::Compact));
  }
}

QVariantList Studio::chatModels() const
{
  QVariantList out;
  for (const auto &p : kProviders) {
    const QString id = QString::fromLatin1(p.id);
    if (QLatin1String(p.kind) != QLatin1String("chat")) continue;
    if (profiles_ && !profiles_->get("providers." + id + ".enabled").toBool()) continue;
    auto it = models_.find(id);
    if (it == models_.end()) continue;
    for (const auto &m : it->second) {
      QVariantMap v = m.toObject().toVariantMap();
      v["provider"] = id;
      v["providerName"] = QString::fromLatin1(p.name);
      v["icon"] = modelIcon(v["id"].toString(), id);
      v["usable"] = id == "ollama" || (profiles_ && profiles_->hasApiKey(id));
      out << v;
    }
  }
  return out;
}

QVariantList Studio::searchModels(const QString &query) const
{
  const QString q = query.trimmed().toLower();
  QVariantList out;
  for (const QVariant &v : chatModels()) {
    const QVariantMap m = v.toMap();
    if (q.isEmpty() || m["id"].toString().toLower().contains(q) ||
        m["name"].toString().toLower().contains(q) ||
        m["providerName"].toString().toLower().contains(q))
    {
      out << m;
    }
  }
  return out;
}

QString Studio::agentProvider() const
{
  return profiles_ ? profiles_->get("agent.provider").toString() : QString();
}

QString Studio::agentModel() const
{
  return profiles_ ? profiles_->get("agent.model").toString() : QString();
}

QString Studio::agentModelLabel() const
{
  const QString provider = agentProvider(), model = agentModel();
  auto it = models_.find(provider);
  if (it != models_.end()) {
    for (const auto &m : it->second) {
      if (m.toObject()["id"].toString() == model) return m.toObject()["name"].toString();
    }
  }
  return model;
}

void Studio::setAgentModel(const QString &provider, const QString &model)
{
  if (!profiles_) return;
  profiles_->set("agent.provider", provider);
  profiles_->set("agent.model", model);
  Q_EMIT agentModelChanged();
}

void Studio::refreshModels(const QString &provider)
{
  for (const auto &p : kProviders) {
    const QString id = QString::fromLatin1(p.id);
    if (!provider.isEmpty() && provider != id) continue;
    if (id == "fal") {
      for (const char *c : {"text-to-image", "image-to-image", "text-to-video", "image-to-video",
                            "image-to-3d", "text-to-3d"})
      {
        fetchFalModels(QString::fromLatin1(c));
      }
      continue;
    }
    fetchModels(id);
  }
}

static bool openai_chat_model(const QString &id)
{
  const QString l = id.toLower();
  static const char *skip[] = {"embedding", "whisper", "tts", "dall-e", "moderation", "davinci",
                               "babbage", "audio", "realtime", "transcribe", "image", "search",
                               "codex-mini", "computer-use", "sora"};
  for (const char *s : skip) {
    if (l.contains(QLatin1String(s))) return false;
  }
  return l.startsWith("gpt-") || l.startsWith("o1") || l.startsWith("o3") || l.startsWith("o4") ||
         l.startsWith("o5") || l.startsWith("chatgpt-");
}

void Studio::fetchModels(const QString &provider)
{
  QNetworkRequest req;
  const QString k = key(provider);
  if (provider == "anthropic") {
    if (k.isEmpty()) return;
    req.setUrl(QUrl("https://api.anthropic.com/v1/models?limit=1000"));
    req.setRawHeader("x-api-key", k.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
  }
  else if (provider == "openai") {
    if (k.isEmpty()) return;
    req.setUrl(QUrl("https://api.openai.com/v1/models"));
    req.setRawHeader("Authorization", "Bearer " + k.toUtf8());
  }
  else if (provider == "google") {
    if (k.isEmpty()) return;
    req.setUrl(QUrl("https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000"));
    req.setRawHeader("x-goog-api-key", k.toUtf8());
  }
  else if (provider == "openrouter") {
    req.setUrl(QUrl("https://openrouter.ai/api/v1/models?supported_parameters=tools"));
  }
  else if (provider == "ollama") {
    QString base = chatBase("ollama");
    base.chop(3); /* "/v1" */
    req.setUrl(QUrl(base + "/api/tags"));
  }
  else {
    return;
  }
  req.setTransferTimeout(20000);
  ++loading_;
  Q_EMIT modelsChanged();
  QNetworkReply *rep = net_->get(req);
  connect(rep, &QNetworkReply::finished, this, [this, rep, provider]() {
    rep->deleteLater();
    --loading_;
    const QByteArray data = rep->readAll();
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    if (rep->error() != QNetworkReply::NoError) {
      QString msg = root["error"].toObject()["message"].toString();
      if (msg.isEmpty()) msg = rep->errorString();
      model_errors_[provider] = msg;
      Q_EMIT modelsChanged();
      Q_EMIT providersChanged();
      return;
    }
    QJsonArray out;
    if (provider == "anthropic") {
      for (const auto &v : root["data"].toArray()) {
        const QJsonObject m = v.toObject();
        out << QJsonObject{{"id", m["id"]}, {"name", m["display_name"].toString(m["id"].toString())},
                           {"vision", true}, {"tools", true}, {"context", 200000}};
      }
    }
    else if (provider == "openai") {
      for (const auto &v : root["data"].toArray()) {
        const QString id = v.toObject()["id"].toString();
        if (!openai_chat_model(id)) continue;
        out << QJsonObject{{"id", id}, {"name", id}, {"vision", true}, {"tools", true}};
      }
    }
    else if (provider == "google") {
      for (const auto &v : root["models"].toArray()) {
        const QJsonObject m = v.toObject();
        bool generates = false;
        for (const auto &meth : m["supportedGenerationMethods"].toArray()) {
          generates |= meth.toString() == "generateContent";
        }
        QString id = m["name"].toString();
        id.remove(0, id.indexOf('/') + 1);
        if (!generates || !id.startsWith("gemini")) continue;
        out << QJsonObject{{"id", id}, {"name", m["displayName"].toString(id)}, {"vision", true},
                           {"tools", true}, {"context", m["inputTokenLimit"].toInt()}};
      }
    }
    else if (provider == "openrouter") {
      for (const auto &v : root["data"].toArray()) {
        const QJsonObject m = v.toObject();
        bool vision = false;
        for (const auto &mod : m["architecture"].toObject()["input_modalities"].toArray()) {
          vision |= mod.toString() == "image";
        }
        out << QJsonObject{{"id", m["id"]}, {"name", m["name"]}, {"vision", vision}, {"tools", true},
                           {"context", m["context_length"].toInt()},
                           {"price_in", m["pricing"].toObject()["prompt"].toString().toDouble() * 1e6},
                           {"price_out", m["pricing"].toObject()["completion"].toString().toDouble() * 1e6},
                           {"free", m["id"].toString().endsWith(":free")}};
      }
      /* OpenRouter's routers aren't in the tools-filtered listing but do call tools: the free
       * router picks a free model that can, the auto router the best paid one. */
      out.prepend(QJsonObject{{"id", "openrouter/auto"}, {"name", "Auto router (best model per request)"},
                              {"vision", true}, {"tools", true}, {"context", 2000000}});
      out.prepend(QJsonObject{{"id", "openrouter/free"}, {"name", "Free router (free models with tools)"},
                              {"vision", true}, {"tools", true}, {"context", 200000}, {"free", true},
                              {"price_in", 0.0}, {"price_out", 0.0}});
    }
    else if (provider == "ollama") {
      for (const auto &v : root["models"].toArray()) {
        const QString id = v.toObject()["name"].toString();
        out << QJsonObject{{"id", id}, {"name", id}, {"vision", false}, {"tools", true}};
      }
    }
    std::vector<QJsonObject> sorted;
    for (const auto &v : out) sorted.push_back(v.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
      const bool ra = a["id"].toString().startsWith("openrouter/"), rb = b["id"].toString().startsWith("openrouter/");
      if (ra != rb) return ra;
      return a["name"].toString().compare(b["name"].toString(), Qt::CaseInsensitive) < 0;
    });
    QJsonArray ordered;
    for (const auto &o : sorted) ordered.append(o);
    models_[provider] = ordered;
    model_errors_.erase(provider);
    saveModelCache(provider);
    Q_EMIT modelsChanged();
    Q_EMIT providersChanged();
    Q_EMIT agentModelChanged();
  });
}

void Studio::testProvider(const QString &provider)
{
  if (provider == "fal") {
    if (key("fal").isEmpty()) {
      Q_EMIT providerTested(provider, false, tr("No key"));
      return;
    }
    QNetworkRequest req(QUrl("https://rest.fal.ai/storage/auth/token?storage_type=fal-cdn-v3"));
    req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(20000);
    QNetworkReply *rep = net_->post(req, "{}");
    connect(rep, &QNetworkReply::finished, this, [this, rep, provider]() {
      rep->deleteLater();
      const bool ok = rep->error() == QNetworkReply::NoError;
      Q_EMIT providerTested(provider, ok, ok ? tr("Connected") : rep->errorString());
      if (ok) refreshModels(provider);
    });
    return;
  }
  /* A model listing is the cheapest authenticated call each provider documents. */
  connect(this, &Studio::providersChanged, this, [this, provider]() {
    const bool ok = !model_errors_.count(provider) && models_.count(provider);
    Q_EMIT providerTested(provider, ok,
                          ok ? tr("%1 models available").arg(models_[provider].size())
                             : model_errors_[provider]);
  }, Qt::SingleShotConnection);
  fetchModels(provider);
}

/* -------------------------------------------------------------------- */
/** \name fal.ai media catalog
 * \{ */

static QString kind_for_category(const QString &c)
{
  if (c.endsWith("image")) return "image";
  if (c.endsWith("video")) return "video";
  if (c.endsWith("3d")) return "3d";
  return QString();
}

void Studio::loadCatalog()
{
  /* Curated families with their input fields (converted from the Openfield add-on's schema
   * snapshot), keyed by endpoint. */
  QFile f(":/harness/data/fal_catalog.json");
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonArray fams = QJsonDocument::fromJson(f.readAll()).object()["families"].toArray();
  for (const auto &fv : fams) {
    const QJsonObject fam = fv.toObject();
    const QString job = fam["job_type"].toString();
    static const QStringList tools = {"sam_3_3d", "sam_3_3d_body", "3d_rigging",
                                      "meshy_v5_retexture", "meshy_v5_remesh", "kimodo",
                                      "kling3_0_motion_control"};
    for (const auto &rv : fam["routes"].toArray()) {
      const QJsonObject r = rv.toObject();
      const QString ep = r["endpoint"].toString();
      if (ep.contains("reference-to-video")) continue;
      catalog_[ep] = QJsonObject{{"family", job},
                                 {"kind", fam["kind"]},
                                 {"name", fam["name"]},
                                 {"provider", fam["provider"]},
                                 {"tool", tools.contains(job)},
                                 {"params", r["params"]}};
    }
  }
}

void Studio::fetchFalModels(const QString &category)
{
  if (key("fal").isEmpty()) return;
  QUrl url("https://api.fal.ai/v1/models");
  QUrlQuery q;
  q.addQueryItem("category", category);
  q.addQueryItem("status", "active");
  q.addQueryItem("limit", "100");
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setRawHeader("Authorization", "Key " + key("fal").toUtf8());
  req.setTransferTimeout(20000);
  ++loading_;
  QNetworkReply *rep = net_->get(req);
  connect(rep, &QNetworkReply::finished, this, [this, rep, category]() {
    rep->deleteLater();
    --loading_;
    if (rep->error() != QNetworkReply::NoError) {
      model_errors_["fal"] = rep->errorString();
      Q_EMIT providersChanged();
      return;
    }
    const QJsonObject root = QJsonDocument::fromJson(rep->readAll()).object();
    QJsonArray &list = fal_models_[kind_for_category(category)];
    for (const auto &v : root["models"].toArray()) {
      const QJsonObject m = v.toObject();
      const QString id = m["endpoint_id"].toString();
      bool dup = false;
      for (const auto &e : list) dup |= e.toObject()["id"].toString() == id;
      if (dup || id.isEmpty()) continue;
      const QJsonObject meta = m["metadata"].toObject();
      list << QJsonObject{{"id", id},
                          {"name", meta["display_name"].toString(id)},
                          {"category", category},
                          {"thumbnail", meta["thumbnail_url"].toString()}};
    }
    models_["fal"] = QJsonArray();
    for (auto &[k, l] : fal_models_) {
      for (const auto &e : l) models_["fal"].append(e);
    }
    model_errors_.erase("fal");
    Q_EMIT createModelsChanged();
    Q_EMIT providersChanged();
  });
}

QJsonObject Studio::schemaFor(const QString &endpoint) const
{
  return catalog_[endpoint].toObject();
}

static QJsonObject resolve_ref(const QJsonObject &doc, QJsonObject s)
{
  for (int guard = 0; guard < 8 && s.contains("$ref"); guard++) {
    const QString name = s["$ref"].toString().section('/', -1);
    s = doc["components"].toObject()["schemas"].toObject()[name].toObject();
  }
  for (const char *k : {"anyOf", "oneOf", "allOf"}) {
    if (s.contains(k)) {
      for (const auto &o : s[k].toArray()) {
        QJsonObject r = resolve_ref(doc, o.toObject());
        if (r["type"].toString() != "null") {
          for (auto it = s.begin(); it != s.end(); ++it) {
            if (it.key() != k) r[it.key()] = it.value();
          }
          return r;
        }
      }
    }
  }
  return s;
}

void Studio::fetchFalSchema(const QString &endpoint)
{
  if (catalog_.contains(endpoint)) return;
  QUrl url("https://fal.ai/api/openapi/queue/openapi.json");
  QUrlQuery q;
  q.addQueryItem("endpoint_id", endpoint);
  url.setQuery(q);
  QNetworkRequest req(url);
  req.setTransferTimeout(20000);
  QNetworkReply *rep = net_->get(req);
  connect(rep, &QNetworkReply::finished, this, [this, rep, endpoint]() {
    rep->deleteLater();
    const QJsonObject doc = QJsonDocument::fromJson(rep->readAll()).object();
    const QJsonObject post = doc["paths"].toObject()["/" + endpoint].toObject()["post"].toObject();
    if (post.isEmpty()) return;
    const QJsonObject schema = resolve_ref(
        doc, post["requestBody"].toObject()["content"].toObject()["application/json"]
                 .toObject()["schema"].toObject());
    const QJsonArray required = schema["required"].toArray();
    QJsonArray params;
    QJsonArray order = schema["x-fal-order-properties"].toArray();
    const QJsonObject props = schema["properties"].toObject();
    if (order.isEmpty()) {
      for (auto it = props.begin(); it != props.end(); ++it) order << it.key();
    }
    for (const auto &kv : order) {
      const QString k = kv.toString();
      const QJsonObject p = resolve_ref(doc, props[k].toObject());
      QJsonObject item{{"key", k}, {"title", p["title"].toString(k)}, {"type", p["type"].toString("string")},
                       {"required", required.contains(k)}};
      for (const char *x : {"enum", "default", "minimum", "maximum"}) {
        if (p.contains(x)) item[x] = p[x];
      }
      if (k.endsWith("_url") || k.endsWith("_urls")) item["media"] = true;
      params << item;
    }
    QString kind = "image";
    for (const auto &[kk, list] : fal_models_) {
      for (const auto &e : list) {
        if (e.toObject()["id"].toString() == endpoint) kind = kk;
      }
    }
    catalog_[endpoint] = QJsonObject{{"family", endpoint}, {"kind", kind}, {"name", endpoint},
                                     {"provider", "fal"}, {"params", params}};
    Q_EMIT createChanged();
  });
}

/** \} */

}  // namespace harness
