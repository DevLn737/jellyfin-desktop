#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <functional>

class ProcessSamplerTest;

namespace DesktopDiagnostics {
// Process identity and counters stay internal; only fixed numeric metrics are emitted.
struct ProcessCounter {
  quint64 creationTicks = 0;
  quint64 cpuTicks = 0;
  quint64 privateBytes = 0;
  qint64 observedNs = 0;
  bool valid = false;
};
struct ProcessSnapshot {
  ProcessCounter parent;
  QHash<quint32, ProcessCounter> children;
  bool childrenAvailable = false;
  double sampleMs = 0;
};

// All methods and delivery run on the owner thread. The collector owns no QObject.
class ProcessSampler final : public QObject {
  Q_OBJECT
public:
  using Collector = std::function<ProcessSnapshot()>;
  explicit ProcessSampler(QObject* parent = nullptr, Collector collector = {});
  void startCapture(int logicalCpuCount);
  void stopCapture();
  bool requestSample();
signals:
  void sampleReady(const QJsonObject& metrics);
private:
  friend class ::ProcessSamplerTest;
  static ProcessSnapshot collect();
  // Validate each queried candidate through an accepted direct parent before
  // flattening the process tree. OS PIDs and creation times remain internal.
  static QHash<quint32, ProcessCounter> selectDescendants(
    quint32 rootPid, const ProcessCounter& root,
    const QHash<quint32, quint32>& parents,
    const QHash<quint32, ProcessCounter>& candidates);
  QJsonObject metrics(const ProcessSnapshot& current) const;
  void consume();
  Collector m_collector;
  QFutureWatcher<ProcessSnapshot> m_future;
  ProcessSnapshot m_previous;
  quint64 m_generation = 0;
  quint64 m_pendingGeneration = 0;
  int m_logicalCpuCount = 1;
  bool m_active = false;
  // Remains true after the worker finishes until its queued result is consumed.
  bool m_busy = false;
};
}
