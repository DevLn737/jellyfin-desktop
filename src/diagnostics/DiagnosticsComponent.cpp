#include "DiagnosticsComponent.h"
#include "DiagnosticsScope.h"
#include "player/PlayerComponent.h"
#include "player/QtHelper.h"
#include "profiling/Trace.h"
#include "Version.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

using DesktopDiagnostics::Event;
DiagnosticsComponent::DiagnosticsComponent(QObject* parent) : ComponentBase(parent) {}
DiagnosticsComponent::~DiagnosticsComponent()
{
  DesktopDiagnostics::setCaptureTarget(nullptr);
  m_active.store(false);
  m_processSampler.stopCapture();
  m_export.waitForFinished();
}

bool DiagnosticsComponent::componentInitialize()
{
  m_heartbeat.setInterval(100);
  m_heartbeat.setTimerType(Qt::PreciseTimer);
  connect(&m_heartbeat, &QTimer::timeout, this, [this] {
    const auto now = m_clock.elapsed();
    record(Event::Gui, {{"delayMs", double(std::max(qint64(0), now - m_lastBeat - 100))}});
    m_lastBeat = now;
    if (now >= DesktopDiagnostics::Buffer::SessionMs) stopCapture();
  });
  connect(&m_processSampler, &DesktopDiagnostics::ProcessSampler::sampleReady, this,
    [this](const QJsonObject& metrics) { record(Event::Process, metrics); });
  m_sample.setInterval(1000);
  connect(&m_sample, &QTimer::timeout, this, &DiagnosticsComponent::sample);
  connect(&m_export, &QFutureWatcher<bool>::finished, this, [this] { emit reportSaved(m_export.result()); });
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
    stopCapture();
    m_export.waitForFinished();
  });
  return true;
}

void DiagnosticsComponent::componentPostInitialize()
{
  auto& player = PlayerComponent::Get();
  connect(&player, &PlayerComponent::buffering, this, [this](float percent) { record(Event::Buffering, {{"percent", double(percent)}}); });
  connect(&player, &PlayerComponent::stateChanged, this, [this](PlayerComponent::State state, PlayerComponent::State) { record(Event::State, {{"state", int(state)}}); });
}

void DiagnosticsComponent::startCapture()
{
  if (capturing()) return;
  {
    QMutexLocker lock(&m_mutex);
    m_buffer.clear();
    m_metadata = {{"schema", 1}, {"session", QUuid::createUuid().toString(QUuid::WithoutBraces)},
      {"startedUtcMs", double(QDateTime::currentMSecsSinceEpoch())},
      {"appVersion", Version::GetVersionString()}, {"qtVersion", QString::fromLatin1(qVersion())},
      {"mpvClientApi", double(mpv_client_api_version())}, {"logicalCpuCount", QThread::idealThreadCount()},
      {"mpvBuild", QStringLiteral(JMP_MPV_VERSION)},
      {"commit", QStringLiteral(JMP_BUILD_COMMIT)}};
    m_clock.start();
    m_lastBeat = m_stoppedMs = 0;
  }
  m_active.store(true, std::memory_order_relaxed);
  m_processSampler.startCapture(QThread::idealThreadCount());
  DesktopDiagnostics::setCaptureTarget(this);
  PlayerComponent::Get().setDiagnosticObservation(true);
  m_heartbeat.start();
  m_sample.start();
  emit captureChanged(true);
}

void DiagnosticsComponent::stopCapture()
{
  if (!m_active.exchange(false, std::memory_order_relaxed)) return;
  m_processSampler.stopCapture();
  DesktopDiagnostics::setCaptureTarget(nullptr);
  PlayerComponent::Get().setDiagnosticObservation(false);
  m_stoppedMs = std::min(m_clock.elapsed(), DesktopDiagnostics::Buffer::SessionMs);
  m_heartbeat.stop();
  m_sample.stop();
  emit captureChanged(false);
}

void DiagnosticsComponent::clearCapture()
{
  stopCapture();
  QMutexLocker lock(&m_mutex);
  m_buffer.clear();
  m_metadata = {};
}

void DiagnosticsComponent::record(Event event, const QJsonObject& metrics)
{
  if (!capturing()) return;
  QMutexLocker lock(&m_mutex);
  if (capturing()) m_buffer.append(m_clock.elapsed(), event, metrics);
}
void DiagnosticsComponent::recordCommand(int command, double durationMs)
{
  record(Event::Command, {{"command", command}, {"durationMs", durationMs}});
}
void DiagnosticsComponent::recordWebChannelLatency(double durationMs)
{
  JMP_PROFILE_ZONE("WebChannel diagnostic round trip");
  if (std::isfinite(durationMs) && durationMs >= 0 && durationMs <= 60000)
    record(Event::WebChannel, {{"durationMs", durationMs}});
}
bool DiagnosticsComponent::ping() const
{
  JMP_PROFILE_ZONE("WebChannel ping dispatch");
  return true;
}
void DiagnosticsComponent::recordSyncPlayEvent(const QString& event, const QVariantMap& metrics)
{
  if (!capturing()) return;
  // Values are protocol enums; neither arbitrary event names nor payloads are stored.
  const QStringList names{"join", "leave", "pause", "resume", "seek", "rate", "buffering", "reconnect", "command", "error", "drift"};
  const int id = names.indexOf(event);
  if (id < 0) return;
  QJsonObject clean{{"event", id}};
  for (const auto& key : {"positionMs", "driftMs", "playbackRate", "status", "rttMs", "whenUtcMs"}) {
    const auto v = metrics.value(QString::fromLatin1(key));
    // toDouble on a string would admit strings containing private input. Accept only native numbers.
    if (v.typeId() != QMetaType::Double && v.typeId() != QMetaType::Int && v.typeId() != QMetaType::LongLong) continue;
    clean.insert(QString::fromLatin1(key), v.toDouble());
  }
  record(Event::SyncPlay, clean);
}

void DiagnosticsComponent::sample()
{
  JMP_PROFILE_ZONE("Diagnostics sample");
  if (!capturing()) return;
  const qint64 now = m_clock.elapsed();
  if (now >= DesktopDiagnostics::Buffer::SessionMs) { stopCapture(); return; }
  // At most one OS snapshot runs; a pending snapshot skips this timer tick.
  m_processSampler.requestSample();
  QElapsedTimer cost;
  cost.start();
  QJsonObject playback = QJsonObject::fromVariantMap(PlayerComponent::Get().diagnosticMetrics());
  playback.insert("sampleMs", cost.nsecsElapsed() / 1000000.0);
  record(Event::Playback, playback);
}

void DiagnosticsComponent::saveReport()
{
  if (m_saveDialogOpen || m_export.isRunning() || m_metadata.isEmpty()) { emit reportSaved(false); return; }
  // QFileDialog runs a nested event loop. Hold this guard until the export
  // future is assigned so repeated WebChannel requests cannot create workers.
  QScopedValueRollback<bool> dialogGuard(m_saveDialogOpen, true);
  const auto path = QFileDialog::getSaveFileName(nullptr, tr("Save diagnostic report"), QStringLiteral("jellyfin-desktop-diagnostics.json"), tr("JSON report (*.json)"));
  if (path.isEmpty()) return;
  DesktopDiagnostics::Buffer frozen;
  QJsonObject metadata;
  {
    QMutexLocker lock(&m_mutex);
    if (m_metadata.isEmpty()) { lock.unlock(); emit reportSaved(false); return; }
    metadata = m_metadata;
    frozen = m_buffer.freeze(capturing() ? std::min(m_clock.elapsed(), DesktopDiagnostics::Buffer::SessionMs) : m_stoppedMs);
    metadata.insert("droppedRecords", double(m_buffer.dropped()));
    metadata.insert("capturing", capturing());
  }
  // QtConcurrent owns copies of the immutable snapshot. Serialize/write outside the GUI thread.
  m_export.setFuture(QtConcurrent::run([path, metadata, frozen] {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QByteArray header = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    header.chop(1);
    header += ",\"records\":[";
    if (file.write(header) != header.size()) return false;
    if (!frozen.writeRecords(file)) return false;
    if (file.write("]}", 2) != 2) return false;
    return file.commit();
  }));
}
