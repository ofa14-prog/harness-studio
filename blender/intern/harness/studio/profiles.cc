/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 */

#include "profiles.hh"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace harness {

static constexpr int kVersion = 1;
static const char *kColors[] = {"#7C8CFF", "#3DD6B0", "#FFB547", "#FF6B8A", "#5EC2FF", "#B28CFF"};

Profiles *Profiles::instance()
{
  static Profiles *p = new Profiles();
  return p;
}

QString Profiles::configDir()
{
  const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                    QStringLiteral("/HarnessStudio");
  QDir().mkpath(d);
  return d;
}

QString Profiles::stateDir()
{
  const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                    QStringLiteral("/HarnessStudio");
  QDir().mkpath(d);
  QFile::setPermissions(d, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  return d;
}

QString Profiles::envNameFor(const QString &provider)
{
  static const QMap<QString, QString> names = {
      {"openai", "OPENAI_API_KEY"},        {"anthropic", "ANTHROPIC_API_KEY"},
      {"google", "GEMINI_API_KEY"},        {"openrouter", "OPENROUTER_API_KEY"},
      {"fal", "FAL_KEY"},                  {"xai", "XAI_API_KEY"},
      {"mistral", "MISTRAL_API_KEY"},      {"deepseek", "DEEPSEEK_API_KEY"},
  };
  return names.value(provider);
}

static bool write_json(const QString &path, const QJsonObject &o, bool secret = false)
{
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
  const bool ok = f.commit();
  if (ok && secret) {
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
  }
  return ok;
}

static QJsonObject read_json(const QString &path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject Profiles::defaults() const
{
  /* The schema: every key a profile can hold, with its default (and so its type). */
  return QJsonObject{
      {"agent", QJsonObject{{"provider", "anthropic"},
                            {"model", "claude-sonnet-5-5"},
                            {"temperature", 0.4},
                            {"max_steps", 40},
                            {"approve_paid", true},
                            {"approve_python", false},
                            {"attach_viewport", true},
                            {"stream", true}}},
      {"create", QJsonObject{{"kind", "image"},
                             {"image_model", "fal-ai/nano-banana-pro"},
                             {"video_model", "bytedance/seedance-2.0/fast/image-to-video"},
                             {"model3d_model", "fal-ai/hunyuan-3d/v3.1/pro/image-to-3d"},
                             {"source", "viewport"},
                             {"auto_import", true}, {"show_presets", true}}},
      {"providers", QJsonObject{{"anthropic", QJsonObject{{"enabled", true}}},
                                {"openai", QJsonObject{{"enabled", true}}},
                                {"google", QJsonObject{{"enabled", true}}},
                                {"openrouter", QJsonObject{{"enabled", true}}},
                                {"fal", QJsonObject{{"enabled", true}}},
                                {"ollama", QJsonObject{{"enabled", false},
                                                       {"url", "http://127.0.0.1:11434"}}}}},
      {"interface", QJsonObject{{"language", "en"},
                                {"studio_width", 380},
                                {"left_width", 300},
                                {"reduce_motion", false},
                                {"show_status_bar", true},
                                {"confirm_quit", true},
                                {"reopen_last", true}}},
  };
}

static QJsonObject merge(const QJsonObject &schema, const QJsonObject &value)
{
  QJsonObject out = schema;
  for (auto it = schema.begin(); it != schema.end(); ++it) {
    if (!value.contains(it.key())) {
      continue;
    }
    const QJsonValue v = value[it.key()];
    if (it->isObject() && v.isObject()) {
      out[it.key()] = merge(it->toObject(), v.toObject());
    }
    else if (it->type() == v.type() || (it->isDouble() && v.isDouble())) {
      out[it.key()] = v;
    }
  }
  return out;
}

void Profiles::load()
{
  index_ = read_json(configDir() + "/profiles.json");
  if (index_["version"].toInt() != kVersion || index_["profiles"].toArray().isEmpty()) {
    index_ = QJsonObject{{"version", kVersion}, {"profiles", QJsonArray()}};
    const QString user = qEnvironmentVariable("USER");
    create(user.isEmpty() ? QStringLiteral("Studio") : user.left(1).toUpper() + user.mid(1));
    return;
  }
  QString id = index_["current"].toString();
  bool found = false;
  for (const auto &p : index_["profiles"].toArray()) {
    found |= p.toObject()["id"].toString() == id;
  }
  if (!found) {
    id = index_["profiles"].toArray().first().toObject()["id"].toString();
  }
  switchTo(id);
  save_timer_.setSingleShot(true);
  save_timer_.setInterval(400);
  connect(&save_timer_, &QTimer::timeout, this, &Profiles::saveProfileNow, Qt::UniqueConnection);
}

QVariantList Profiles::profiles() const
{
  return index_["profiles"].toArray().toVariantList();
}

QVariantMap Profiles::current() const
{
  for (const auto &p : index_["profiles"].toArray()) {
    if (p.toObject()["id"].toString() == current_id_) {
      return p.toObject().toVariantMap();
    }
  }
  return {};
}

QJsonObject Profiles::readProfile(const QString &id) const
{
  return read_json(configDir() + "/profiles/" + id + ".json");
}

QString Profiles::create(const QString &name, const QString &email)
{
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  QJsonArray list = index_["profiles"].toArray();
  const QString clean = name.trimmed().isEmpty() ? QStringLiteral("Profile") : name.trimmed();
  list.append(QJsonObject{{"id", id},
                          {"name", clean},
                          {"email", email.trimmed()},
                          {"color", kColors[list.size() % 6]},
                          {"created", QDateTime::currentDateTime().toString(Qt::ISODate)}});
  index_["profiles"] = list;
  QDir().mkpath(configDir() + "/profiles");
  write_json(configDir() + "/profiles/" + id + ".json",
             QJsonObject{{"version", kVersion}, {"settings", defaults()}, {"usage", QJsonObject()}});
  switchTo(id);
  Q_EMIT profilesChanged();
  return id;
}

void Profiles::switchTo(const QString &id)
{
  if (save_timer_.isActive()) {
    save_timer_.stop();
    saveProfileNow();
  }
  current_id_ = id;
  index_["current"] = id;
  saveIndex();
  const QJsonObject file = readProfile(id);
  settings_ = merge(defaults(), file["settings"].toObject());
  usage_ = file["usage"].toObject();
  ++keys_revision_;
  Q_EMIT currentChanged();
  Q_EMIT settingsChanged();
  Q_EMIT usageChanged();
  Q_EMIT keysChanged();
}

bool Profiles::remove(const QString &id)
{
  QJsonArray list = index_["profiles"].toArray();
  if (list.size() <= 1) {
    return false; /* Always keep one profile. */
  }
  for (int i = 0; i < list.size(); i++) {
    if (list[i].toObject()["id"].toString() == id) {
      list.removeAt(i);
      break;
    }
  }
  index_["profiles"] = list;
  QFile::remove(configDir() + "/profiles/" + id + ".json");
  QFile::remove(stateDir() + "/keys-" + id + ".json");
  if (id == current_id_) {
    switchTo(list.first().toObject()["id"].toString());
  }
  saveIndex();
  Q_EMIT profilesChanged();
  return true;
}

void Profiles::updateProfile(const QString &field, const QVariant &value)
{
  if (field != "name" && field != "email" && field != "color") {
    return;
  }
  QJsonArray list = index_["profiles"].toArray();
  for (int i = 0; i < list.size(); i++) {
    QJsonObject p = list[i].toObject();
    if (p["id"].toString() == current_id_) {
      QString v = value.toString().trimmed();
      if (field == "name" && v.isEmpty()) {
        return;
      }
      p[field] = v;
      list[i] = p;
    }
  }
  index_["profiles"] = list;
  saveIndex();
  Q_EMIT profilesChanged();
  Q_EMIT currentChanged();
}

QVariant Profiles::get(const QString &key) const
{
  QJsonValue v = settings_;
  for (const QString &part : key.split('.')) {
    v = v.toObject().value(part);
  }
  return v.toVariant();
}

static bool set_path(QJsonObject &root, const QStringList &parts, int i, const QJsonValue &v)
{
  if (!root.contains(parts[i])) {
    return false;
  }
  if (i == parts.size() - 1) {
    const QJsonValue old = root[parts[i]];
    if (old.isObject() || old.isArray()) {
      return false;
    }
    if (old.type() != v.type() && !(old.isDouble() && v.isDouble())) {
      return false;
    }
    root[parts[i]] = v;
    return true;
  }
  QJsonObject child = root[parts[i]].toObject();
  if (!set_path(child, parts, i + 1, v)) {
    return false;
  }
  root[parts[i]] = child;
  return true;
}

bool Profiles::set(const QString &key, const QVariant &value)
{
  QJsonValue v = QJsonValue::fromVariant(value);
  if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong) {
    v = QJsonValue(value.toDouble());
  }
  QJsonObject next = settings_;
  if (!set_path(next, key.split('.'), 0, v)) {
    return false; /* Unknown key or wrong type: nothing changes. */
  }
  if (next == settings_) {
    return true;
  }
  settings_ = next;
  saveProfileSoon();
  Q_EMIT settingsChanged();
  return true;
}

static void collect_keys(const QJsonObject &o, const QString &prefix, QStringList &out)
{
  for (auto it = o.begin(); it != o.end(); ++it) {
    const QString k = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
    if (it->isObject()) collect_keys(it->toObject(), k, out);
    else out << k;
  }
}

QStringList Profiles::keys() const
{
  QStringList out;
  collect_keys(settings_, QString(), out);
  return out;
}

void Profiles::saveProfileSoon()
{
  save_timer_.start();
}

void Profiles::saveProfileNow()
{
  if (current_id_.isEmpty()) {
    return;
  }
  QDir().mkpath(configDir() + "/profiles");
  write_json(configDir() + "/profiles/" + current_id_ + ".json",
             QJsonObject{{"version", kVersion}, {"settings", settings_}, {"usage", usage_}});
}

void Profiles::saveIndex()
{
  write_json(configDir() + "/profiles.json", index_);
}

QJsonObject Profiles::readKeys() const
{
  return read_json(stateDir() + "/keys-" + current_id_ + ".json");
}

void Profiles::writeKeys(const QJsonObject &keys) const
{
  write_json(stateDir() + "/keys-" + current_id_ + ".json", keys, true);
}

QString Profiles::apiKey(const QString &provider) const
{
  const QString env = envNameFor(provider);
  if (!env.isEmpty() && qEnvironmentVariableIsSet(env.toLatin1().constData())) {
    return qEnvironmentVariable(env.toLatin1().constData()).trimmed();
  }
  return readKeys()[provider].toString();
}

void Profiles::setApiKey(const QString &provider, const QString &key)
{
  QJsonObject keys = readKeys();
  keys[provider] = key.trimmed();
  writeKeys(keys);
  ++keys_revision_;
  Q_EMIT keysChanged();
}

void Profiles::clearApiKey(const QString &provider)
{
  QJsonObject keys = readKeys();
  keys.remove(provider);
  writeKeys(keys);
  ++keys_revision_;
  Q_EMIT keysChanged();
}

bool Profiles::hasApiKey(const QString &provider) const
{
  return !apiKey(provider).isEmpty();
}

QString Profiles::apiKeySource(const QString &provider) const
{
  const QString env = envNameFor(provider);
  if (!env.isEmpty() && qEnvironmentVariableIsSet(env.toLatin1().constData())) {
    return QStringLiteral("env");
  }
  return readKeys().contains(provider) ? QStringLiteral("file") : QString();
}

QString Profiles::maskedApiKey(const QString &provider) const
{
  const QString k = apiKey(provider);
  if (k.size() < 8) {
    return k.isEmpty() ? QString() : QStringLiteral("••••");
  }
  return k.left(4) + QStringLiteral(" •••• ") + k.right(4);
}

void Profiles::addUsage(const QString &provider, int input_tokens, int output_tokens, double cost)
{
  QJsonObject p = usage_[provider].toObject();
  p["input_tokens"] = p["input_tokens"].toDouble() + input_tokens;
  p["output_tokens"] = p["output_tokens"].toDouble() + output_tokens;
  p["cost_usd"] = p["cost_usd"].toDouble() + cost;
  p["requests"] = p["requests"].toDouble() + 1;
  usage_[provider] = p;
  saveProfileSoon();
  Q_EMIT usageChanged();
}

}  // namespace harness
