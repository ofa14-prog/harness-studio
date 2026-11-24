/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * Profiles and settings, after the layout Craft Anything uses: one versioned settings file
 * with a fixed schema (unknown keys and wrong types are refused, nothing half-written), a file
 * per profile for that person's choices, and secrets kept apart from both — API keys live in a
 * private 0600 file per profile (or come from the provider's environment variable) and are
 * never written into settings or shown again in full.
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace harness {

class Profiles : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
  Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
  Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
  Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
  Q_PROPERTY(QVariantMap usage READ usage NOTIFY usageChanged)
  Q_PROPERTY(int keysRevision READ keysRevision NOTIFY keysChanged)

 public:
  static Profiles *instance();
  void load();

  QVariantList profiles() const;
  QString currentId() const { return current_id_; }
  QVariantMap current() const;
  QVariantMap settings() const { return settings_.toVariantMap(); }
  QVariantMap usage() const { return usage_.toVariantMap(); }
  int keysRevision() const { return keys_revision_; }

  /* Profiles. */
  Q_INVOKABLE QString create(const QString &name, const QString &email = QString());
  Q_INVOKABLE void switchTo(const QString &id);
  Q_INVOKABLE bool remove(const QString &id);
  Q_INVOKABLE void updateProfile(const QString &field, const QVariant &value);

  /* Profile settings, by dotted key ("agent.model", "providers.openai.enabled"...). */
  Q_INVOKABLE QVariant get(const QString &key) const;
  Q_INVOKABLE bool set(const QString &key, const QVariant &value);
  Q_INVOKABLE QStringList keys() const;

  /* Secrets. */
  Q_INVOKABLE void setApiKey(const QString &provider, const QString &key);
  Q_INVOKABLE void clearApiKey(const QString &provider);
  Q_INVOKABLE bool hasApiKey(const QString &provider) const;
  Q_INVOKABLE QString maskedApiKey(const QString &provider) const;
  Q_INVOKABLE QString apiKeySource(const QString &provider) const;
  QString apiKey(const QString &provider) const;

  /* Usage bookkeeping (local only). */
  void addUsage(const QString &provider, int input_tokens, int output_tokens, double cost_usd);

  static QString configDir();
  static QString stateDir();
  static QString envNameFor(const QString &provider);

 Q_SIGNALS:
  void profilesChanged();
  void currentChanged();
  void settingsChanged();
  void usageChanged();
  void keysChanged();

 private:
  QJsonObject defaults() const;
  QJsonObject readProfile(const QString &id) const;
  void saveProfileSoon();
  void saveProfileNow();
  void saveIndex();
  QJsonObject readKeys() const;
  void writeKeys(const QJsonObject &keys) const;

  QJsonObject index_;      /* {version, current, profiles:[{id,name,email,color,created}]} */
  QString current_id_;
  QJsonObject settings_;   /* current profile's settings */
  QJsonObject usage_;
  QTimer save_timer_;
  int keys_revision_ = 0;
};

}  // namespace harness
