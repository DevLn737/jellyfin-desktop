#include "ProcessSampler.h"
#include "profiling/Trace.h"
#include <QSet>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace DesktopDiagnostics {
namespace {
qint64 monotonicNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool cpuPercent(const ProcessCounter& current, const ProcessCounter& previous,
                int logicalCpuCount, double& value)
{
  if (!current.valid || !previous.valid || current.creationTicks != previous.creationTicks
      || current.cpuTicks < previous.cpuTicks || current.observedNs <= previous.observedNs) return false;
  // Windows CPU counters use 100 ns ticks. Use the collector's actual observation
  // interval, independent of timer scheduling and delayed GUI delivery.
  value = double(current.cpuTicks - previous.cpuTicks) * 100.0
    / (double(current.observedNs - previous.observedNs) / 100.0)
    / logicalCpuCount;
  return std::isfinite(value) && value >= 0;
}
#ifdef Q_OS_WIN
quint64 fileTicks(FILETIME time)
{
  return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
ProcessCounter processNumbers(HANDLE process)
{
  FILETIME creation, exit, kernel, user;
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  ProcessCounter result;
  if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) return result;
  const auto observedNs = monotonicNs();
  if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
    result.creationTicks = fileTicks(creation);
    result.cpuTicks = fileTicks(kernel) + fileTicks(user);
    result.privateBytes = memory.PrivateUsage;
    result.observedNs = observedNs;
    result.valid = true;
  }
  return result;
}
#endif
}

ProcessSampler::ProcessSampler(QObject* parent, Collector collector)
  : QObject(parent), m_collector(collector ? std::move(collector) : Collector(&ProcessSampler::collect))
{
  connect(&m_future, &QFutureWatcher<ProcessSnapshot>::finished, this, &ProcessSampler::consume);
}
void ProcessSampler::startCapture(int logicalCpuCount)
{
  Q_ASSERT(QThread::currentThread() == thread());
  ++m_generation;
  m_active = true;
  m_logicalCpuCount = std::max(1, logicalCpuCount);
  m_previous = {};
}
void ProcessSampler::stopCapture()
{
  Q_ASSERT(QThread::currentThread() == thread());
  ++m_generation;
  m_active = false;
  m_previous = {};
}
bool ProcessSampler::requestSample()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (!m_active || m_busy) return false;
  m_busy = true;
  m_pendingGeneration = m_generation;
  const auto collector = m_collector;
  // The future only owns this copied function. Destroying the sampler disconnects
  // delivery and does not wait for OS enumeration or retain a component pointer.
  m_future.setFuture(QtConcurrent::run([collector] {
    JMP_PROFILE_ZONE("Diagnostics process collection");
    return collector();
  }));
  return true;
}
void ProcessSampler::consume()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (!m_busy || !m_future.isFinished()) return;
  const auto generation = m_pendingGeneration;
  ProcessSnapshot current;
  try { current = m_future.result(); }
  catch (...) { m_busy = false; return; }
  m_busy = false;
  if (!m_active || generation != m_generation) return;
  const auto values = metrics(current);
  m_previous = std::move(current);
  emit sampleReady(values);
}
QJsonObject ProcessSampler::metrics(const ProcessSnapshot& current) const
{
  QJsonObject result;
  double cpu = 0;
  if (current.parent.valid) {
    result.insert("privateBytes", double(current.parent.privateBytes));
    if (cpuPercent(current.parent, m_previous.parent, m_logicalCpuCount, cpu))
      result.insert("cpuPercent", cpu);
  }
  if (current.childrenAvailable && current.parent.valid) {
    quint64 bytes = 0;
    int count = 0;
    double childCpu = 0;
    for (auto it = current.children.cbegin(); it != current.children.cend(); ++it) {
      // An older orphan can retain a parent PID later reused by this application.
      // A real descendant cannot have been born before the current root process.
      if (!it.value().valid || it.value().creationTicks < current.parent.creationTicks) continue;
      ++count;
      const auto childBytes = it.value().privateBytes;
      if (childBytes <= std::numeric_limits<quint64>::max() - bytes) bytes += childBytes;
      if (cpuPercent(it.value(), m_previous.children.value(it.key()), m_logicalCpuCount, cpu)) childCpu += cpu;
    }
    result.insert("childPrivateBytes", double(bytes));
    result.insert("childCount", count);
    if (m_previous.childrenAvailable && m_previous.parent.valid
        && m_previous.parent.creationTicks == current.parent.creationTicks
        && std::isfinite(childCpu)) result.insert("childCpuPercent", childCpu);
  }
  if (std::isfinite(current.sampleMs) && current.sampleMs >= 0) result.insert("sampleMs", current.sampleMs);
  return result;
}
ProcessSnapshot ProcessSampler::collect()
{
  const auto startedNs = monotonicNs();
  ProcessSnapshot result;
#ifdef Q_OS_WIN
  result.parent = processNumbers(GetCurrentProcess());
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    QHash<DWORD, DWORD> parents;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const bool populated = Process32FirstW(snapshot, &entry);
    if (populated) do { parents.insert(entry.th32ProcessID, entry.th32ParentProcessID); } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (populated) {
      QSet<DWORD> descendants{GetCurrentProcessId()};
      bool changed = true;
      while (changed) {
        changed = false;
        for (auto it = parents.cbegin(); it != parents.cend(); ++it)
          if (descendants.contains(it.value()) && !descendants.contains(it.key())) {
            descendants.insert(it.key()); changed = true;
          }
      }
      descendants.remove(GetCurrentProcessId());
      for (DWORD pid : descendants) {
        HANDLE child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!child) continue;
        const auto counter = processNumbers(child);
        CloseHandle(child);
        if (counter.valid) result.children.insert(pid, counter);
      }
      result.childrenAvailable = true;
    }
  }
#endif
  result.sampleMs = double(monotonicNs() - startedNs) / 1000000.0;
  return result;
}
}
