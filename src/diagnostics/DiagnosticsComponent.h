#pragma once
#include "ComponentManager.h"
#include "DiagnosticsBuffer.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMutex>
#include <QTimer>
#include <QHash>
#include <atomic>

class DiagnosticsComponent : public ComponentBase {
  Q_OBJECT
  DEFINE_SINGLETON(DiagnosticsComponent);
  Q_PROPERTY(bool capturing READ capturing NOTIFY captureChanged)
public:
  explicit DiagnosticsComponent(QObject* parent = nullptr);
  ~DiagnosticsComponent() override;
  bool componentInitialize() override;
  void componentPostInitialize() override;
  const char* componentName() override { return "diagnostics"; }
  bool componentExport() override { return true; }
  bool capturing() const { return m_active.load(std::memory_order_relaxed); }
  Q_INVOKABLE void startCapture();
  Q_INVOKABLE void stopCapture();
  Q_INVOKABLE void clearCapture();
  Q_INVOKABLE void saveReport();
  Q_INVOKABLE void recordSyncPlayEvent(const QString& event, const QVariantMap& metrics);
  Q_INVOKABLE void recordWebChannelLatency(double durationMs);
  Q_INVOKABLE bool ping() const;
  void recordCommand(int command, double durationMs);
signals:
  void captureChanged(bool active);
  void reportSaved(bool success);
private:
  void record(DesktopDiagnostics::Event event, const QJsonObject& metrics);
  void sample();
  std::atomic<bool> m_active{false};
  QMutex m_mutex;
  DesktopDiagnostics::Buffer m_buffer;
  QElapsedTimer m_clock;
  QTimer m_heartbeat;
  QTimer m_sample;
  qint64 m_lastBeat = 0;
  qint64 m_stoppedMs = 0;
  qint64 m_lastSample = 0;
  quint64 m_cpuPrevious = 0;
  QHash<quint32, quint64> m_childCpuPrevious;
  QJsonObject m_metadata;
  QFutureWatcher<bool> m_export;
  bool m_saveDialogOpen = false;
};
