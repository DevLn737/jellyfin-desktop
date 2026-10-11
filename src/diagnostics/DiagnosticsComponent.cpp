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
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

using DesktopDiagnostics::Event;
DiagnosticsComponent::DiagnosticsComponent(QObject* parent) : ComponentBase(parent) {}
DiagnosticsComponent::~DiagnosticsComponent()
{
  DesktopDiagnostics::setCaptureTarget(nullptr);
  m_active.store(false);
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
    m_lastBeat = m_lastSample = m_stoppedMs = 0;
    m_cpuPrevious = 0;
    m_childCpuPrevious.clear();
  }
  m_active.store(true, std::memory_order_relaxed);
  DesktopDiagnostics::setCaptureTarget(this);
  PlayerComponent::Get().setDiagnosticObservation(true);
  m_heartbeat.start();
  m_sample.start();
  emit captureChanged(true);
}

void DiagnosticsComponent::stopCapture()
{
  if (!m_active.exchange(false, std::memory_order_relaxed)) return;
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

#ifdef Q_OS_WIN
static quint64 fileTicks(FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
static bool processNumbers(HANDLE process, quint64& ticks, quint64& bytes)
{
  FILETIME creation, exit, kernel, user;
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)
      || !GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) return false;
  ticks = fileTicks(kernel) + fileTicks(user);
  bytes = memory.PrivateUsage;
  return true;
}
#endif
void DiagnosticsComponent::sample()
{
  JMP_PROFILE_ZONE("Diagnostics sample");
  if (!capturing()) return;
  const qint64 now = m_clock.elapsed();
  if (now >= DesktopDiagnostics::Buffer::SessionMs) { stopCapture(); return; }
  QElapsedTimer cost;
  cost.start();
  QJsonObject process;
#ifdef Q_OS_WIN
  const auto cpuPercent = [this, now](quint64 delta) {
    return double(delta) / 10000.0 / double(std::max(qint64(1), now - m_lastSample))
      / std::max(1, QThread::idealThreadCount()) * 100.0;
  };
  quint64 ticks = 0, bytes = 0;
  if (processNumbers(GetCurrentProcess(), ticks, bytes)) {
    process.insert("privateBytes", double(bytes));
    if (m_cpuPrevious) process.insert("cpuPercent", cpuPercent(ticks - m_cpuPrevious));
    m_cpuPrevious = ticks;
  }
  // Aggregate descendants without recording process names, command lines, PIDs or paths.
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    QHash<DWORD, DWORD> parents;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) do { parents.insert(entry.th32ProcessID, entry.th32ParentProcessID); } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    QSet<DWORD> descendants{GetCurrentProcessId()};
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto it = parents.cbegin(); it != parents.cend(); ++it)
        if (descendants.contains(it.value()) && !descendants.contains(it.key())) { descendants.insert(it.key()); changed = true; }
    }
    descendants.remove(GetCurrentProcessId());
    quint64 childBytes = 0, childDelta = 0;
    int count = 0;
    QHash<quint32, quint64> current;
    for (DWORD pid : descendants) {
      HANDLE child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
      if (!child) continue;
      if (processNumbers(child, ticks, bytes)) {
        ++count; childBytes += bytes; current.insert(pid, ticks);
        if (m_childCpuPrevious.contains(pid) && ticks >= m_childCpuPrevious.value(pid)) childDelta += ticks - m_childCpuPrevious.value(pid);
      }
      CloseHandle(child);
    }
    process.insert("childPrivateBytes", double(childBytes));
    process.insert("childCount", count);
    if (m_lastSample) process.insert("childCpuPercent", cpuPercent(childDelta));
    m_childCpuPrevious = current;
  }
#endif
  process.insert("sampleMs", cost.nsecsElapsed() / 1000000.0);
  record(Event::Process, process);
  cost.restart();
  QJsonObject playback = QJsonObject::fromVariantMap(PlayerComponent::Get().diagnosticMetrics());
  playback.insert("sampleMs", cost.nsecsElapsed() / 1000000.0);
  record(Event::Playback, playback);
  m_lastSample = now;
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
