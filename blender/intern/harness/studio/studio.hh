/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * Harness Studio: the AI agent and the Create (generation) studio in the right-hand panel.
 *
 * Providers are spoken to in their own documented formats:
 * - Anthropic Messages API (`/v1/messages`, `x-api-key`, `anthropic-version`), SSE streaming.
 * - OpenAI Chat Completions (`/v1/chat/completions`, Bearer), SSE streaming with usage.
 * - Google Gemini through its official OpenAI-compatible endpoint (`/v1beta/openai/`); model list
 *   from the native `/v1beta/models`.
 * - OpenRouter (`/api/v1/chat/completions`), its public `/api/v1/models` listing.
 * - Ollama (local) through `/v1` with its `/api/tags` listing.
 * - fal.ai for media: queue (`queue.fal.run`), CDN uploads (`rest.fal.ai/storage/auth/token`),
 *   model listing (`api.fal.ai/v1/models`) and per-endpoint OpenAPI schemas.
 *
 * The agent works on the live scene through #Bridge (Python in Blender's main loop).
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <map>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace harness {

class Profiles;

struct ToolCall {
  QString id, name;
  QString args_json;
  QJsonObject args;
};

class Studio : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
  Q_PROPERTY(QVariantList chatModels READ chatModels NOTIFY modelsChanged)
  Q_PROPERTY(bool modelsLoading READ modelsLoading NOTIFY modelsChanged)
  Q_PROPERTY(QString agentProvider READ agentProvider NOTIFY agentModelChanged)
  Q_PROPERTY(QString agentModel READ agentModel NOTIFY agentModelChanged)
  Q_PROPERTY(QString agentModelLabel READ agentModelLabel NOTIFY agentModelChanged)
  Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged)
  Q_PROPERTY(QString threadId READ threadId NOTIFY threadChanged)
  Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString activity READ activity NOTIFY busyChanged)
  /** What the agent is doing now, from its real steps: thinking, reading, writing, tool,
   * approval. With the tool's name, the step number and when this phase began (ms). */
  Q_PROPERTY(QString activityPhase READ activityPhase NOTIFY busyChanged)
  Q_PROPERTY(QString activityTool READ activityTool NOTIFY busyChanged)
  Q_PROPERTY(int activityStep READ activityStep NOTIFY busyChanged)
  Q_PROPERTY(int activityMaxSteps READ activityMaxSteps NOTIFY busyChanged)
  Q_PROPERTY(qint64 activitySince READ activitySince NOTIFY busyChanged)
  Q_PROPERTY(QString createKind READ createKind WRITE setCreateKind NOTIFY createChanged)
  Q_PROPERTY(QString createModel READ createModel WRITE setCreateModel NOTIFY createChanged)
  Q_PROPERTY(QString createSource READ createSource WRITE setCreateSource NOTIFY createChanged)
  Q_PROPERTY(QString createFile READ createFile WRITE setCreateFile NOTIFY createChanged)
  Q_PROPERTY(QVariantList createModels READ createModels NOTIFY createModelsChanged)
  Q_PROPERTY(QVariantList createParams READ createParams NOTIFY createChanged)
  Q_PROPERTY(bool createAcceptsImage READ createAcceptsImage NOTIFY createChanged)
  /** {name, icon, family} of the chosen Create model, whichever of its family's routes it is. */
  Q_PROPERTY(QVariantMap createModelInfo READ createModelInfo NOTIFY createChanged)
  /** Scene link options for Create: pass (beauty|depth|normal|edges|mask), followCamera,
   * applyMaterial, place (cursor|object|origin|none), refs ([{kind,name}]), region. */
  Q_PROPERTY(QVariantMap createOptions READ createOptions NOTIFY createChanged)
  /** The chosen video model has an end-frame input (first/last frame references). */
  Q_PROPERTY(bool createTakesEndFrame READ createTakesEndFrame NOTIFY createChanged)
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
  Q_PROPERTY(int presetsRevision READ presetsRevision NOTIFY presetsChanged)
  Q_PROPERTY(bool creating READ creating NOTIFY jobsChanged)

 public:
  static Studio *instance();
  void attach(Profiles *profiles);

  QVariantList providers() const;
  QVariantList chatModels() const;
  bool modelsLoading() const { return loading_ > 0; }
  QString agentProvider() const;
  QString agentModel() const;
  QString agentModelLabel() const;
  QVariantList threads() const;
  QString threadId() const { return thread_id_; }
  QVariantList messages() const { return messages_; }
  bool busy() const { return busy_; }
  QString activity() const { return activity_; }
  QString activityPhase() const { return activity_phase_; }
  QString activityTool() const { return activity_tool_; }
  int activityStep() const { return steps_; }
  int activityMaxSteps() const;
  qint64 activitySince() const { return activity_since_; }

  QString createKind() const { return create_kind_; }
  void setCreateKind(const QString &kind);
  QString createModel() const;
  void setCreateModel(const QString &endpoint);
  QString createSource() const { return create_source_; }
  void setCreateSource(const QString &s) { create_source_ = s; Q_EMIT createChanged(); }
  QString createFile() const { return create_file_; }
  void setCreateFile(const QString &f);
  QVariantList createModels() const;
  QVariantList createParams() const;
  bool createAcceptsImage() const;
  QVariantMap createModelInfo() const;
  QVariantMap createOptions() const { return create_options_; }
  bool createTakesEndFrame() const;
  QVariantList jobs() const;
  int presetsRevision() const { return presets_revision_; }
  bool creating() const;

  /* Models. */
  Q_INVOKABLE void refreshModels(const QString &provider = QString());
  Q_INVOKABLE void testProvider(const QString &provider);
  Q_INVOKABLE void setAgentModel(const QString &provider, const QString &model);
  Q_INVOKABLE QVariantList searchModels(const QString &query) const;
  Q_INVOKABLE QString providerIcon(const QString &provider) const;
  /** The brand mark of the company that makes `model` (a Simple Icons slug shipped in
   * resources/icons/brands), else the provider's mark, else empty. */
  Q_INVOKABLE QString modelIcon(const QString &model, const QString &provider = QString()) const;
  Q_INVOKABLE QString providerName(const QString &provider) const;

  /* Agent. */
  /** `refs`: the @ references in the text, [{kind, name}]: their live detail goes with it. */
  Q_INVOKABLE void send(const QString &text, bool attach_viewport, const QVariantList &refs = {},
                        const QStringList &attachments = {});
  Q_INVOKABLE void stop();
  Q_INVOKABLE void newThread();
  Q_INVOKABLE void openThread(const QString &id);
  Q_INVOKABLE void deleteThread(const QString &id);
  Q_INVOKABLE void answerApproval(int message_index, bool approve);
  Q_INVOKABLE void retry();
  /** Write the conversation (what the panel shows and what the model saw) to a JSON file. */
  Q_INVOKABLE void dumpThread(const QString &path) const;

  /* Create. */
  Q_INVOKABLE void create(const QString &prompt);
  Q_INVOKABLE void setCreateParam(const QString &key, const QVariant &value);
  Q_INVOKABLE void cancelJob(int id);
  Q_INVOKABLE void useJob(int id, const QString &as);
  Q_INVOKABLE void removeJob(int id);
  Q_INVOKABLE void revealJob(int id);
  Q_INVOKABLE void setCreateOption(const QString &key, const QVariant &value);
  /** Scene link actions on a finished job. */
  Q_INVOKABLE void applyJobMaterial(int id, const QVariantList &refs = {});
  Q_INVOKABLE void matchJobLighting(int id);
  Q_INVOKABLE void placeJob(int id, const QString &at, const QString &target = QString());
  /** Start picking an area of the viewport for a regional edit. */
  Q_INVOKABLE void pickRegion();
  /** A preset's picture made with fal.ai (file URL), or empty: the shipped render is used. */
  Q_INVOKABLE QString presetPreview(const QString &id) const;
  /** Make real previews for presets with the cheapest fast image model on fal.ai
   * ([{id, prompt, cat}]); each lands in the preview cache when done. */
  Q_INVOKABLE void generatePresetPreviews(const QVariantList &presets);
  Q_INVOKABLE bool hasKey(const QString &provider) const { return !key(provider).isEmpty(); }

 Q_SIGNALS:
  void providersChanged();
  void modelsChanged();
  void agentModelChanged();
  void threadsChanged();
  void threadChanged();
  void messagesChanged();
  void busyChanged();
  void createChanged();
  void createModelsChanged();
  void jobsChanged();
  void providerTested(const QString &provider, bool ok, const QString &message);
  void notify(const QString &kind, const QString &text);
  void presetsChanged();

 private:
  struct Job {
    int id = 0;
    QString kind, endpoint, title, prompt, status, message, file, url;
    double progress = 0.0;
    qint64 created = 0;
    QString status_url, response_url, cancel_url;
    bool auto_import = false;
    QString place = QStringLiteral("cursor"), place_target; /* where an imported 3D result goes */
    QJsonArray material_refs;                                 /* dress these in an image result */
    QString preset_id;                                        /* a preset preview, not a result */
  };

  /* Providers & models. */
  QString key(const QString &provider) const;
  QString chatBase(const QString &provider) const;
  void fetchModels(const QString &provider);
  void loadModelCache();
  void saveModelCache(const QString &provider);

  /* Agent loop. */
  void step();
  void requestAnthropic();
  void requestOpenAICompatible();
  QJsonArray toolSchemas(bool anthropic) const;
  QString systemPrompt() const;
  void handleStreamChunk(const QByteArray &chunk);
  void finishStep(const QString &stop_reason);
  bool retryAfterError(int http_status, const QString &message);
  int retries_ = 0;
  void runTools();
  void runTool(const ToolCall &call, std::function<void(QJsonObject result, QString image)> done);
  void appendMessage(const QVariantMap &m);
  void updateLastAssistant(const QString &text, bool streaming);
  void setBusy(bool b, const QString &activity = QString(),
               const QString &phase = QStringLiteral("thinking"), const QString &tool = QString());
  void failTurn(const QString &message);
  void saveThread();
  void loadThreads();
  QString threadsDir() const;
  /* Each project file keeps its own conversation: project path -> thread id. */
  void projectChanged(const QString &path);
  QJsonObject projectThreads() const;
  void rememberProjectThread();
  QString current_project_;
  bool project_known_ = false;

  /* Create. */
  int startJob(const QString &kind, const QString &endpoint, const QString &prompt,
               const QJsonObject &input, const QString &image_path, bool auto_import,
               const QMap<QString, QString> &extra_media = {});
  void uploadAll(QStringList keys, QMap<QString, QString> media, QJsonObject body, int id,
                 std::function<void(QJsonObject)> done);
  void createFromScene(const QString &ep, const QString &prompt, const QJsonObject &input,
                       bool auto_import);
  QString routeFor(const QString &endpoint, bool has_image) const;
  void configureJob(int id, const QVariantMap &opts);
  QString mediaParam(const QString &route, const QStringList &needles) const;
  void uploadToFal(const QString &path, std::function<void(QString url, QString error)> done);
  void submitJob(int id, const QJsonObject &body);
  void pollJobs();
  void fetchJobResult(int id);
  void finishJob(int id, bool ok, const QString &message);
  Job *job(int id);
  void loadCatalog();
  void fetchFalModels(const QString &category);
  void fetchFalSchema(const QString &endpoint);
  QJsonObject schemaFor(const QString &endpoint) const;
  void captureViewport(std::function<void(QString path, QString error)> done);
  QString outputsDir() const;

  Profiles *profiles_ = nullptr;
  QNetworkAccessManager *net_ = nullptr;
  int loading_ = 0;
  std::map<QString, QJsonArray> models_;       /* provider -> [{id,name,context,vision,tools}] */
  std::map<QString, QString> model_errors_;

  /* Conversation. */
  QString thread_id_;
  QString thread_title_;
  QVariantList messages_;          /* What the panel shows. */
  QJsonArray history_;             /* Provider-neutral transcript (OpenAI-style messages). */
  bool busy_ = false;
  QString activity_;
  QString activity_phase_;
  QString activity_tool_;
  qint64 activity_since_ = 0;
  QPointer<QNetworkReply> reply_;
  QByteArray sse_buffer_;
  QString stream_text_;
  QString stream_event_;
  std::vector<ToolCall> pending_calls_;
  std::map<int, int> stream_tool_index_;  /* provider index -> pending_calls_ index */
  int steps_ = 0;
  int input_tokens_ = 0, output_tokens_ = 0;
  QString pending_image_;          /* Viewport capture to attach to the next request. */
  std::map<int, std::function<void(bool)>> approvals_;
  QStringList recent_tool_signatures_;

  /* Create. */
  QString create_kind_ = QStringLiteral("image");
  QString create_source_ = QStringLiteral("viewport");
  QString create_file_;
  std::map<QString, QVariantMap> create_values_;
  QVariantMap create_options_{{"pass", "beauty"}, {"followCamera", false}, {"applyMaterial", false},
                              {"place", "cursor"}, {"refs", QVariantList()}, {"region", false}};
  QJsonObject catalog_;                        /* endpoint -> {kind,name,provider,params,...} */
  std::map<QString, QJsonArray> fal_models_;   /* kind -> listing */
  std::vector<Job> jobs_;
  int next_job_ = 1;
  QTimer poll_timer_;
  QTimer save_timer_;
  /* The library survives restarts: library.json beside the files (and any result file left
   * there without a record), unfinished queue jobs resume polling. */
  void saveJobs() const;
  QStringList removed_files_;
  int presets_revision_ = 0;   /* removed from the list: not brought back by the folder scan */
  void loadJobs();
};

}  // namespace harness
