#include <QtTest>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QSemaphore>
#include <QSet>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include "diagnostics/ProcessSampler.h"
using namespace DesktopDiagnostics;

namespace {
struct Gate {
  QSemaphore entered;
  QSemaphore release;
  QSemaphore returned;
  std::atomic<int> calls{0};
  std::atomic<bool> onWorker{false};
};
struct ReleaseOnExit {
  std::shared_ptr<Gate> gate;
  ~ReleaseOnExit() { gate->release.release(); }
};
ProcessSnapshot snapshot(quint64 ticks, qint64 ns, quint64 creation = 1)
{
  ProcessSnapshot result;
  result.parent = {creation, ticks, 8192, ns, true};
  result.childrenAvailable = true;
  result.sampleMs = 0.25;
  return result;
}
QJsonObject value(const QSignalSpy& spy, int index)
{
  return qvariant_cast<QJsonObject>(spy.at(index).at(0));
}
}

class ProcessSamplerTest : public QObject {
  Q_OBJECT
private slots:
  void delayedWorkerLeavesGuiResponsiveAndOneFlight() {
    const auto gate = std::make_shared<Gate>();
    ReleaseOnExit release{gate};
    QThread* guiThread = QThread::currentThread();
    ProcessSampler sampler(nullptr, [gate, guiThread] {
      ++gate->calls;
      gate->onWorker.store(QThread::currentThread() != guiThread);
      gate->entered.release();
      gate->release.acquire();
      return snapshot(100, 1000000000);
    });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    bool deliveredOnGui = false;
    connect(&sampler, &ProcessSampler::sampleReady, this, [&] { deliveredOnGui = QThread::currentThread() == guiThread; });
    sampler.startCapture(2);
    QVERIFY(sampler.requestSample());
    QVERIFY(gate->entered.tryAcquire(1, 2000));
    QVERIFY(!sampler.requestSample());
    int pulses = 0;
    QTimer pulse;
    connect(&pulse, &QTimer::timeout, this, [&] { ++pulses; });
    pulse.start(5);
    QTRY_VERIFY_WITH_TIMEOUT(pulses >= 3, 2000);
    QCOMPARE(spy.size(), 0);
    QCOMPARE(gate->calls.load(), 1);
    QVERIFY(gate->onWorker.load());
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 2000);
    QVERIFY(deliveredOnGui);
  }
  void completedWorkerCannotReplaceUndeliveredResult() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    ProcessSampler sampler(nullptr, [calls] {
      const int call = ++*calls;
      return snapshot(quint64(call) * 1000000, qint64(call) * 1000000000);
    });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(2);
    QVERIFY(sampler.requestSample());
    // Wait for the actual future without delivering its queued finished signal.
    sampler.m_future.future().waitForFinished();
    QVERIFY(sampler.m_future.isFinished());
    QCOMPARE(spy.size(), 0);
    QVERIFY(!sampler.requestSample());
    QCOMPARE(calls->load(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 2000);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 2000);
    QCOMPARE(value(spy, 1).value("cpuPercent").toDouble(), 5.0);
  }
  void stopRestartDiscardsLateResultAndItsCpuBaseline() {
    const auto gate = std::make_shared<Gate>();
    ReleaseOnExit release{gate};
    ProcessSampler sampler(nullptr, [gate] {
      const int call = ++gate->calls;
      if (call == 1) {
        gate->entered.release();
        gate->release.acquire();
        return snapshot(5000000, 1000000000);
      }
      return snapshot(quint64(call - 1) * 1000000, qint64(call - 1) * 1000000000);
    });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(2);
    QVERIFY(sampler.requestSample());
    QVERIFY(gate->entered.tryAcquire(1, 2000));
    sampler.stopCapture();
    sampler.startCapture(2);
    QVERIFY(!sampler.requestSample());
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!sampler.m_busy, 2000);
    QCOMPARE(spy.size(), 0);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 2000);
    QVERIFY(!value(spy, 0).contains("cpuPercent"));
    QVERIFY(!value(spy, 0).contains("childCpuPercent"));
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 2000);
    QCOMPARE(value(spy, 1).value("cpuPercent").toDouble(), 5.0);
  }
  void stopDiscardsAlreadyCompletedQueuedResult() {
    ProcessSampler sampler(nullptr, [] { return snapshot(1000000, 1000000000); });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(1);
    QVERIFY(sampler.requestSample());
    sampler.m_future.future().waitForFinished();
    sampler.stopCapture();
    QTRY_VERIFY_WITH_TIMEOUT(!sampler.m_busy, 2000);
    QCOMPARE(spy.size(), 0);
    QVERIFY(!sampler.requestSample());
  }
  void destructionDoesNotWaitForOrCaptureOwner() {
    const auto gate = std::make_shared<Gate>();
    ReleaseOnExit release{gate};
    int callbacks = 0;
    auto sampler = std::make_unique<ProcessSampler>(nullptr, [gate] {
      gate->entered.release();
      // A regression that waits in destruction fails within a bounded time
      // instead of deadlocking the test runner before its release guard runs.
      gate->release.tryAcquire(1, 2000);
      gate->returned.release();
      return snapshot(100, 1000000000);
    });
    connect(sampler.get(), &ProcessSampler::sampleReady, this, [&](const QJsonObject&) { ++callbacks; });
    sampler->startCapture(1);
    QVERIFY(sampler->requestSample());
    QVERIFY(gate->entered.tryAcquire(1, 2000));
    QElapsedTimer destruction;
    destruction.start();
    sampler.reset();
    QVERIFY2(destruction.elapsed() < 250, "Destruction must not wait for a blocked collector");
    gate->release.release();
    QVERIFY(gate->returned.tryAcquire(1, 2000));
    QTest::qWait(30);
    QCOMPARE(callbacks, 0);
  }
  void cpuUsesWorkerTimesAndRejectsPidReuseAndCounterRegression() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    ProcessSampler sampler(nullptr, [calls] {
      const int call = ++*calls;
      auto current = snapshot(quint64(call) * 1000000, qint64(call) * 1000000000);
      // Child PID 77 was reused between snapshots. Child 88 is the same process.
      current.children.insert(77, {quint64(call), quint64(call) * 4000000, 4096, qint64(call) * 1000000000, true});
      current.children.insert(88, {9, quint64(call) * 2000000, 4096, qint64(call) * 1000000000, true});
      if (call == 3) {
        current.parent.cpuTicks = 1;
        current.children[88].observedNs = 1000000000; // Clock/counter regression is rejected.
        current.sampleMs = std::numeric_limits<double>::infinity();
      }
      return current;
    });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(2);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 2000);
    QVERIFY(sampler.requestSample());
    sampler.m_future.future().waitForFinished();
    // Actual collector timestamps remain one second apart despite late delivery.
    QThread::msleep(60);
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 2000);
    QCOMPARE(value(spy, 1).value("cpuPercent").toDouble(), 5.0);
    QCOMPARE(value(spy, 1).value("childCpuPercent").toDouble(), 10.0);
    QCOMPARE(value(spy, 1).value("childCount").toInt(), 2);
    QCOMPARE(value(spy, 1).value("childPrivateBytes").toDouble(), 8192.0);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 3, 2000);
    const auto last = value(spy, 2);
    QVERIFY(!last.contains("cpuPercent"));
    QVERIFY(!last.contains("sampleMs"));
    QCOMPARE(last.value("childCpuPercent").toDouble(), 0.0);
    const QSet<QString> allowed{"cpuPercent", "privateBytes", "childPrivateBytes", "childCpuPercent", "childCount", "sampleMs"};
    for (int i = 0; i < spy.size(); ++i) {
      const auto metrics = value(spy, i);
      for (auto it = metrics.begin(); it != metrics.end(); ++it) {
        QVERIFY(allowed.contains(it.key()));
        QVERIFY(it.value().isDouble());
        QVERIFY(std::isfinite(it.value().toDouble()));
      }
    }
  }
  void failedCollectorDoesNotWedgeOneFlight() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    ProcessSampler sampler(nullptr, [calls] {
      if (++*calls == 1) throw std::runtime_error("collector failed");
      return snapshot(1000000, 1000000000);
    });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(1);
    QVERIFY(sampler.requestSample());
    QTRY_VERIFY_WITH_TIMEOUT(!sampler.m_busy, 2000);
    QCOMPARE(spy.size(), 0);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 2000);
    QVERIFY(!value(spy, 0).contains("cpuPercent"));
  }
  void olderOrphanIsExcludedFromDescendantCountMemoryAndCpu() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    ProcessSampler sampler(nullptr, [calls] {
      const int call = ++*calls;
      auto current = snapshot(quint64(call) * 1000000, qint64(call) * 1000000000, 10);
      current.children.insert(77, {9, quint64(call) * 8000000, 999999, qint64(call) * 1000000000, true});
      current.children.insert(88, {11, quint64(call) * 2000000, 4096, qint64(call) * 1000000000, true});
      return current;
    });
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(2);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 2000);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 2000);
    for (int i = 0; i < 2; ++i) {
      const auto metrics = value(spy, i);
      QCOMPARE(metrics.value("childCount").toInt(), 1);
      QCOMPARE(metrics.value("childPrivateBytes").toDouble(), 4096.0);
      QVERIFY(!metrics.contains("pid"));
      QVERIFY(!metrics.contains("creationTicks"));
    }
    QCOMPARE(value(spy, 1).value("childCpuPercent").toDouble(), 10.0);
  }
  void defaultCollectorExportsOnlyNumericAllowlistedFields() {
    ProcessSampler sampler;
    QSignalSpy spy(&sampler, &ProcessSampler::sampleReady);
    sampler.startCapture(1);
    QVERIFY(sampler.requestSample());
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 5000);
    const auto metrics = value(spy, 0);
    QVERIFY(metrics.contains("sampleMs"));
    const QSet<QString> allowed{"cpuPercent", "privateBytes", "childPrivateBytes", "childCpuPercent", "childCount", "sampleMs"};
    for (auto it = metrics.begin(); it != metrics.end(); ++it) {
      QVERIFY(allowed.contains(it.key()));
      QVERIFY(it.value().isDouble());
      QVERIFY(std::isfinite(it.value().toDouble()));
    }
#ifdef Q_OS_WIN
    QVERIFY(metrics.value("privateBytes").toDouble() > 0);
#endif
  }
  void validatedTreeExcludesOldOrphanAndItsNewerDescendant() {
    const auto root = snapshot(1000000, 1000000000, 1000).parent;
    const QHash<quint32, quint32> parents{{101, 100}, {102, 101}, {103, 100}};
    const QHash<quint32, ProcessCounter> candidates{
      {101, {500, 8000000, 999999, 1000000000, true}},
      {102, {1500, 8000000, 999999, 1000000000, true}},
      {103, {1100, 1000000, 4096, 1000000000, true}}};
    auto selected = ProcessSampler::selectDescendants(100, root, parents, candidates);
    QCOMPARE(selected.size(), 1);
    QVERIFY(selected.contains(103));
    QVERIFY(!selected.contains(101));
    QVERIFY(!selected.contains(102));
    // Exercise the real aggregation with the helper's output, not a copied filter.
    ProcessSampler sampler;
    sampler.startCapture(2);
    sampler.m_previous = snapshot(1000000, 1000000000, 1000);
    sampler.m_previous.children = selected;
    auto current = snapshot(2000000, 2000000000, 1000);
    auto next = candidates;
    for (auto it = next.begin(); it != next.end(); ++it) {
      it.value().cpuTicks += 1000000;
      it.value().observedNs = 2000000000;
    }
    current.children = ProcessSampler::selectDescendants(100, current.parent, parents, next);
    const auto metrics = sampler.metrics(current);
    QCOMPARE(metrics.value("childCount").toInt(), 1);
    QCOMPARE(metrics.value("childPrivateBytes").toDouble(), 4096.0);
    QCOMPARE(metrics.value("childCpuPercent").toDouble(), 5.0);
  }
  void validatedTreeRejectsParentPidReusedAfterChildBirth() {
    const auto root = snapshot(100, 1000000000, 1000).parent;
    const QHash<quint32, quint32> parents{{101, 100}, {102, 101}, {103, 102}, {104, 101}};
    const QHash<quint32, ProcessCounter> candidates{
      {101, {2000, 100, 4096, 1000000000, true}},
      {102, {1500, 100, 4096, 1000000000, true}},
      {103, {2500, 100, 4096, 1000000000, true}},
      {104, {2200, 100, 4096, 1000000000, true}}};
    const auto selected = ProcessSampler::selectDescendants(100, root, parents, candidates);
    QCOMPARE(selected.size(), 2);
    QVERIFY(selected.contains(101));
    QVERIFY(selected.contains(104));
    QVERIFY(!selected.contains(102));
    QVERIFY(!selected.contains(103));
  }
  void validatedTreeRequiresValidAndCompleteIntermediateCounters() {
    const auto root = snapshot(100, 1000000000, 1000).parent;
    const QHash<quint32, quint32> parents{
      {101, 100}, {102, 101}, {201, 100}, {202, 201}, {401, 100}};
    const QHash<quint32, ProcessCounter> candidates{
      {101, {1100, 100, 4096, 1000000000, false}},
      {102, {1200, 100, 4096, 1000000000, true}},
      // 201 could not be queried; 301 has no PPID snapshot entry.
      {202, {1200, 100, 4096, 1000000000, true}},
      {301, {1200, 100, 4096, 1000000000, true}},
      {401, {1200, 100, 4096, 1000000000, true}}};
    const auto selected = ProcessSampler::selectDescendants(100, root, parents, candidates);
    QCOMPARE(selected.size(), 1);
    QVERIFY(selected.contains(401));
    auto invalidRoot = root;
    invalidRoot.valid = false;
    QVERIFY(ProcessSampler::selectDescendants(100, invalidRoot, parents, candidates).isEmpty());
    QVERIFY(ProcessSampler::selectDescendants(0, root, parents, candidates).isEmpty());
  }
  void validatedTreeAcceptsMultiLevelBranchesRegardlessOfCandidateOrder() {
    const auto root = snapshot(100, 1000000000, 1000).parent;
    const QHash<quint32, quint32> parents{
      {101, 100}, {102, 101}, {103, 102}, {104, 100}, {105, 104}};
    const QList<quint32> orders[] = {{101, 102, 103, 104, 105}, {105, 104, 103, 102, 101}};
    for (const auto& order : orders) {
      QHash<quint32, ProcessCounter> candidates;
      for (quint32 pid : order)
        candidates.insert(pid, {1000 + pid, pid, pid * 16, 1000000000, true});
      const auto selected = ProcessSampler::selectDescendants(100, root, parents, candidates);
      QCOMPARE(selected.size(), 5);
      for (quint32 pid : order) {
        QVERIFY(selected.contains(pid));
        QCOMPARE(selected.value(pid).creationTicks, quint64(1000 + pid));
        QCOMPARE(selected.value(pid).privateBytes, quint64(pid * 16));
      }
    }
  }
  void validatedTreeTerminatesOnCyclesAndNeverReplacesRootSeed() {
    const auto root = snapshot(100, 1000000000, 1000).parent;
    const QHash<quint32, quint32> parents{
      {100, 101}, {101, 100}, {201, 202}, {202, 201}, {301, 301}};
    const QHash<quint32, ProcessCounter> candidates{
      {100, {1, 100, 999999, 1000000000, true}},
      {101, {1100, 100, 4096, 1000000000, true}},
      {201, {1200, 100, 4096, 1000000000, true}},
      {202, {1300, 100, 4096, 1000000000, true}},
      {301, {1400, 100, 4096, 1000000000, true}}};
    const auto selected = ProcessSampler::selectDescendants(100, root, parents, candidates);
    QCOMPARE(selected.size(), 1);
    QVERIFY(selected.contains(101));
    QVERIFY(!selected.contains(100));
    // Equal FILETIME births are valid (clock granularity); older ones are not.
    auto equal = candidates;
    equal[101].creationTicks = 1000;
    QVERIFY(ProcessSampler::selectDescendants(100, root, parents, equal).contains(101));
    equal[101].creationTicks = 999;
    QVERIFY(ProcessSampler::selectDescendants(100, root, parents, equal).isEmpty());
  }
};
QTEST_GUILESS_MAIN(ProcessSamplerTest)
#include "ProcessSamplerTest.moc"
