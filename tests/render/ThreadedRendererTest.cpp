#include "PlayerQuickItem.h"
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QElapsedTimer>
#include <QTimer>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QThread>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>
#include <psapi.h>

// This is the existing production friend seam. Inspection runs only on the
// scenegraph thread; no libmpv commands/events are consumed here.
class RendererTestAccess
{
public:
  static PlayerRenderer* renderer(PlayerQuickItem* item) { return item->m_renderer; }
  static const QOpenGLFramebufferObject* fbo(PlayerQuickItem* item)
  { return item->m_renderer ? item->m_renderer->m_fbo.get() : nullptr; }
  static bool output(PlayerQuickItem* item)
  { return item->m_renderer && item->m_renderer->m_hasFrame; }
  static bool context(PlayerQuickItem* item)
  { return item->m_renderer && item->m_renderer->m_mpvGL; }
  static bool visible(PlayerQuickItem* item)
  { return item->m_renderer && item->m_renderer->m_visible; }
};

static qint64 nowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void maximum(std::atomic<qint64>& target, qint64 value)
{
  auto previous = target.load();
  while (previous < value && !target.compare_exchange_weak(previous, value)) {}
}
struct Metrics
{
  std::atomic<qint64> syncPasses{0}, renderPasses{0}, swaps{0}, outputFrames{0};
  std::atomic<qint64> contextsCreated{0}, contextsInvalidated{0}, fboChanges{0};
  std::atomic<qint64> glErrors{0}, pixelSamples{0}, pixelFailures{0};
  std::atomic<qint64> windowPixelSamples{0}, windowPixelFailures{0}, windowGrabMaximumNs{0};
  std::atomic<qint64> loadedFiles{0}, shownLoads{0}, hiddenFboAllocations{0}, hiddenOutputFrames{0}, showDelayMaximumNs{0};
  std::atomic<qint64> freshVoGeneration{0}, freshPositionGeneration{0};
  std::atomic<qint64> renderMaximumNs{0}, guiHeartbeatMaximumNs{0}, guiPropertyMaximumNs{0};
  std::atomic<qint64> guiSyncMaximumNs{0}, guiSyncCalls{0}, commandErrors{0};
  std::atomic<qint64> renderStuckWarnings{0}, swapMissingWarnings{0}, mpvWarnings{0}, restarts{0};
  std::atomic<qint64> stopRequestNs{0}, stopCompletionNs{0}, closeCallNs{0}, destroyWindowNs{0}, shutdownCoreNs{0};
  std::atomic<double> position{-1};
  std::atomic<bool> exposed{false}, threaded{false}, hardware{false};
  std::atomic<bool> stopRequested{false}, idle{true}, closing{false}, windowDestroyed{false}, coreDestroyed{false};
  std::atomic<bool> guiSyncInProgress{false}, finished{false}, guardExceeded{false};
  std::atomic<bool> voConfigured{false};
  std::mutex informationMutex;
  QString glRenderer, glVersion;
  qint64 startedNs = nowNs();
};

static bool acceptable(const Metrics& metrics, bool captureWindow, bool hiddenStartup)
{
  return metrics.finished && metrics.hardware && metrics.threaded && !metrics.guardExceeded &&
    metrics.outputFrames > 50 && metrics.swaps > 50 && metrics.pixelSamples > 5 && metrics.pixelFailures == 0 &&
    metrics.glErrors == 0 && metrics.commandErrors == 0 && metrics.renderStuckWarnings == 0 && metrics.swapMissingWarnings == 0 &&
    metrics.coreDestroyed && metrics.windowDestroyed &&
    (!captureWindow || (metrics.windowPixelSamples > 5 && metrics.windowPixelFailures == 0)) &&
    (!hiddenStartup || (metrics.loadedFiles == 2 && metrics.shownLoads == 2 &&
      metrics.hiddenFboAllocations == 0 && metrics.hiddenOutputFrames == 0));
}

class Controller
{
public:
  explicit Controller(Metrics& metrics, bool paused) : m_metrics(metrics), m_paused(paused)
  {
    auto ready = m_ready.get_future();
    m_thread = std::thread([this] { run(); });
    m_handle = ready.get();
  }
  ~Controller() { shutdown(); }
  mpv::qt::Handle handle() const { return m_handle; }
  void enqueue(std::initializer_list<QString> values)
  { std::lock_guard<std::mutex> lock(m_mutex); m_commands.emplace_back(values); }
  void shutdown()
  {
    if (!m_thread.joinable()) return;
    const auto begin = nowNs();
    m_handle = {};
    m_stopping = true;
    m_thread.join();
    m_metrics.shutdownCoreNs = nowNs() - begin;
    m_metrics.coreDestroyed = true;
  }
private:
  void run()
  {
    auto handle = mpv::qt::Handle::FromRawHandle(mpv_create());
    if (!handle) { m_ready.set_value({}); return; }
    for (const auto& option : {std::pair{"config", "no"}, {"idle", "yes"},
         {"pause", "no"}, {"vo", "libmpv"}, {"ao", "null"}, {"hwdec", "no"},
         {"loop-file", "inf"}, {"terminal", "no"}, {"osc", "no"}, {"load-scripts", "no"}})
      if (mpv_set_option_string(handle, option.first, option.second) < 0)
      { m_ready.set_value({}); return; }
    mpv_request_log_messages(handle, "warn");
    if (m_paused) mpv_set_option_string(handle, "pause", "yes");
    if (mpv_initialize(handle) < 0) { m_ready.set_value({}); return; }
    mpv_observe_property(handle, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, 2, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, 3, "vo-configured", MPV_FORMAT_FLAG);
    m_ready.set_value(handle);
    qint64 snapshotGeneration = 0, nextSnapshotNs = 0;
    bool voPending = false, positionPending = false;
    while (!m_stopping)
    {
      std::deque<QList<QString>> commands;
      { std::lock_guard<std::mutex> lock(m_mutex); commands.swap(m_commands); }
      for (const auto& command : commands)
      {
        QList<QByteArray> bytes;
        for (const auto& argument : command) bytes.append(argument.toUtf8());
        std::vector<const char*> arguments;
        for (const auto& argument : bytes) arguments.push_back(argument.constData());
        arguments.push_back(nullptr);
        if (mpv_command_async(handle, 0, arguments.data()) < 0) ++m_metrics.commandErrors;
      }
      // Observations may coalesce unchanged values on a paused replacement.
      // Query fresh snapshots through this sole event consumer, never the GUI.
      if (m_paused && snapshotGeneration && nowNs() >= nextSnapshotNs)
      {
        nextSnapshotNs = nowNs() + 100000000;
        if (!voPending && m_metrics.freshVoGeneration != snapshotGeneration)
        {
          voPending = mpv_get_property_async(handle, 1000 + 2*snapshotGeneration,
            "vo-configured", MPV_FORMAT_FLAG) >= 0;
          if (!voPending) ++m_metrics.commandErrors;
        }
        if (!positionPending && m_metrics.freshPositionGeneration != snapshotGeneration)
        {
          positionPending = mpv_get_property_async(handle, 1001 + 2*snapshotGeneration,
            "time-pos", MPV_FORMAT_DOUBLE) >= 0;
          if (!positionPending) ++m_metrics.commandErrors;
        }
      }
      const auto* event = mpv_wait_event(handle, 0.02); // Sole event consumer.
      if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) ++m_metrics.restarts;
      if (event->event_id == MPV_EVENT_FILE_LOADED)
      {
        snapshotGeneration = ++m_metrics.loadedFiles;
        voPending = false; positionPending = false; nextSnapshotNs = 0;
      }
      if (event->event_id == MPV_EVENT_GET_PROPERTY_REPLY && event->reply_userdata >= 1000)
      {
        const auto generation = qint64((event->reply_userdata - 1000) / 2);
        if (generation == snapshotGeneration)
        {
          const bool position = (event->reply_userdata - 1000) % 2 != 0;
          if (position) positionPending = false; else voPending = false;
          const auto* property = static_cast<mpv_event_property*>(event->data);
          if (event->error >= 0 && property && property->data)
          {
            if (position && property->format == MPV_FORMAT_DOUBLE &&
                *static_cast<double*>(property->data) >= 29.5)
              m_metrics.freshPositionGeneration = generation;
            if (!position && property->format == MPV_FORMAT_FLAG && *static_cast<int*>(property->data))
              m_metrics.freshVoGeneration = generation;
          }
        }
      }
      if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0) ++m_metrics.commandErrors;
      if (event->event_id == MPV_EVENT_PROPERTY_CHANGE)
      {
        const auto* property = static_cast<mpv_event_property*>(event->data);
        if (event->reply_userdata == 1 && property->format == MPV_FORMAT_DOUBLE && property->data)
          m_metrics.position = *static_cast<double*>(property->data);
        if (event->reply_userdata == 2 && property->format == MPV_FORMAT_FLAG && property->data)
        {
          m_metrics.idle = *static_cast<int*>(property->data) != 0;
          if (m_metrics.idle && m_metrics.stopRequested && !m_metrics.stopCompletionNs.load())
            m_metrics.stopCompletionNs = nowNs() - m_metrics.stopRequestNs.load();
        }
        if (event->reply_userdata == 3 && property->format == MPV_FORMAT_FLAG && property->data)
          m_metrics.voConfigured = *static_cast<int*>(property->data) != 0;
      }
      if (event->event_id == MPV_EVENT_LOG_MESSAGE)
      {
        ++m_metrics.mpvWarnings;
        const auto* log = static_cast<mpv_event_log_message*>(event->data);
        const QByteArray text(log->text);
        if (text.contains("render_context_render") && (text.contains("not called") || text.contains("stuck")))
          ++m_metrics.renderStuckWarnings;
        if (text.contains("report_swap") && (text.contains("not called") || text.contains("previous frame") || text.contains("missing")))
          ++m_metrics.swapMissingWarnings;
      }
      if (event->event_id == MPV_EVENT_END_FILE)
      {
        const auto* end = static_cast<mpv_event_end_file*>(event->data);
        if (end->error < 0) ++m_metrics.commandErrors;
      }
    }
  }
  Metrics& m_metrics;
  bool m_paused = false;
  mpv::qt::Handle m_handle;
  std::promise<mpv::qt::Handle> m_ready;
  std::thread m_thread;
  std::atomic<bool> m_stopping{false};
  std::mutex m_mutex;
  std::deque<QList<QString>> m_commands;
};

static QJsonObject counters(const Metrics& m)
{
  QJsonObject result;
  const auto number = [&](const char* name, const std::atomic<qint64>& v) { result[name] = double(v.load()); };
  number("syncPasses", m.syncPasses); number("renderPasses", m.renderPasses); number("swaps", m.swaps);
  number("mpvOutputFrames", m.outputFrames); number("contextsCreated", m.contextsCreated);
  number("contextsInvalidated", m.contextsInvalidated); number("fboChanges", m.fboChanges);
  number("glErrors", m.glErrors); number("pixelSamples", m.pixelSamples); number("pixelFailures", m.pixelFailures);
  number("windowPixelSamples", m.windowPixelSamples); number("windowPixelFailures", m.windowPixelFailures);
  number("loadedFiles", m.loadedFiles); number("shownLoads", m.shownLoads);
  number("freshVoGeneration", m.freshVoGeneration); number("freshPositionGeneration", m.freshPositionGeneration);
  number("hiddenFboAllocations", m.hiddenFboAllocations); number("hiddenOutputFrames", m.hiddenOutputFrames);
  number("guiSyncCalls", m.guiSyncCalls); number("commandErrors", m.commandErrors);
  number("renderStuckWarnings", m.renderStuckWarnings); number("mpvWarnings", m.mpvWarnings); number("restarts", m.restarts);
  number("swapMissingWarnings", m.swapMissingWarnings);
  for (const auto& metric : {std::pair{"renderMaximumMs", &m.renderMaximumNs},
      {"guiHeartbeatMaximumMs", &m.guiHeartbeatMaximumNs}, {"guiPropertyMaximumMs", &m.guiPropertyMaximumNs},
      {"guiSyncMaximumMs", &m.guiSyncMaximumNs}, {"stopCompletionMs", &m.stopCompletionNs},
      {"windowGrabMaximumMs", &m.windowGrabMaximumNs},
      {"showDelayMaximumMs", &m.showDelayMaximumNs},
      {"closeCallMs", &m.closeCallNs}, {"destroyWindowMs", &m.destroyWindowNs}, {"shutdownCoreMs", &m.shutdownCoreNs}})
    result[metric.first] = metric.second->load() / 1000000.0;
  result["elapsedSeconds"] = (nowNs() - m.startedNs) / 1e9;
  result["positionSeconds"] = m.position.load();
  result["exposed"] = m.exposed.load(); result["threaded"] = m.threaded.load();
  result["hardwareOpenGL"] = m.hardware.load(); result["stopRequested"] = m.stopRequested.load();
  result["closing"] = m.closing.load(); result["windowDestroyed"] = m.windowDestroyed.load();
  result["coreDestroyed"] = m.coreDestroyed.load(); result["guiSyncInProgress"] = m.guiSyncInProgress.load();
  result["memoryGuardExceeded"] = m.guardExceeded.load();
  return result;
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  const auto args = app.arguments();
  if (args.size() < 4) return 2;
  const auto fixture = args[1], outputPath = args[2];
  const int duration = args[3].toInt();
  const bool guiSync = args.contains("--gui-sync");
  const bool captureWindow = args.contains("--capture-window");
  const bool hiddenStartup = args.contains("--hidden-startup");
  if (duration < 30 || duration > 45) return 2;
  QFile fixtureManifest(QFileInfo(fixture).absolutePath() + "/fixture-info.json");
  if (!fixtureManifest.open(QIODevice::ReadOnly)) return 2;
  const auto fixtureBytes = fixtureManifest.readAll();
  const auto fixtureInfo = QJsonDocument::fromJson(fixtureBytes).object();
  const auto hashes = fixtureInfo["sha256"].toObject();
  for (auto it = hashes.begin(); it != hashes.end(); ++it)
  {
    QFile file(QFileInfo(fixture).absolutePath() + "/" + it.key());
    if (!file.open(QIODevice::ReadOnly) ||
        QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()) != it.value().toString())
      return 2;
  }
  Metrics metrics;
  QJsonObject base{{"schemaVersion", 1}, {"scope", "exposed-threaded-production-renderer-local-h264"},
    {"qualificationEligible", false}, {"desktopApplicationLaunched", false}, {"testWindowShown", true},
    {"guiSynchronousMpvProbe", guiSync}, {"durationSeconds", duration}, {"qtVersion", qVersion()},
    {"qtGrabWindowProbe", captureWindow},
    {"hiddenPausedStartupProbe", hiddenStartup},
    {"sourceCommit", JMP_THREADED_TEST_SOURCE_COMMIT}, {"rendererSourceSha256", JMP_THREADED_RENDERER_SOURCE_SHA256},
    {"rendererHeaderSha256", JMP_THREADED_RENDERER_HEADER_SHA256},
    {"fixtureManifestSha256", QString::fromLatin1(QCryptographicHash::hash(fixtureBytes, QCryptographicHash::Sha256).toHex())},
    {"pixelSource", "production-mpv-fbo; does not establish final-window composite"},
    {"hardwareDecoding", false}, {"memoryLimitBytes", double(2147483648ULL)},
    {"completed", false}, {"passed", false}};
  // A non-GUI reporter retains actual observations if GUI/render/core deadlock.
  // It owns no player pointer and never requests collection or forces GC.
  std::atomic<bool> stopReporter{false};
  std::thread reporter([&] {
    QJsonArray samples;
    while (!stopReporter)
    {
      PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
      if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
      {
        const auto value = counters(metrics);
        samples.append(QJsonObject{{"elapsedSeconds", value["elapsedSeconds"]},
          {"rssBytes", double(memory.WorkingSetSize)}, {"privateBytes", double(memory.PrivateUsage)},
          {"renderPasses", value["renderPasses"]}, {"swaps", value["swaps"]}});
        if (memory.WorkingSetSize > 2147483648ULL || memory.PrivateUsage > 2147483648ULL) metrics.guardExceeded = true;
      }
      auto report = base;
      report["metrics"] = counters(metrics); report["samples"] = samples;
      { std::lock_guard<std::mutex> lock(metrics.informationMutex);
        report["glRenderer"] = metrics.glRenderer; report["glVersion"] = metrics.glVersion; }
      report["completed"] = metrics.finished.load();
      report["passed"] = acceptable(metrics, captureWindow, hiddenStartup);
      report["finalWindowPixelsVerified"] = captureWindow && metrics.windowPixelSamples > 5 && metrics.windowPixelFailures == 0;
      QSaveFile output(outputPath);
      if (output.open(QIODevice::WriteOnly)) { output.write(QJsonDocument(report).toJson()); output.commit(); }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  });
  int result = 1;
  {
    Controller controller(metrics, hiddenStartup);
    if (!controller.handle()) { stopReporter = true; reporter.join(); return 3; }
    auto window = std::make_unique<QQuickWindow>();
    window->setTitle(QStringLiteral("Owned production renderer test (local fixture)"));
    window->setGeometry(80, 80, 1280, 720); window->setColor(Qt::black);
    auto* item = new PlayerQuickItem;
    item->setParentItem(window->contentItem()); item->setSize(window->size());
    item->initMpvHandle(controller.handle()); item->setVideoVisible(!hiddenStartup); item->setPlaybackActive(!hiddenStartup);
    QObject::disconnect(item, &PlayerQuickItem::onFatalError, item, nullptr);
    QObject::connect(item, &PlayerQuickItem::onFatalError, &app, [&](QString) { ++metrics.commandErrors; });
    PlayerRenderer* observedRenderer = nullptr;
    GLuint previousFbo = 0;
    qint64 renderStarted = 0, lastPixels = 0;
    QObject::connect(window.get(), &QQuickWindow::beforeSynchronizing, window.get(), [&] {
      if (metrics.closing) return;
      ++metrics.syncPasses;
      metrics.threaded = QThread::currentThread() != app.thread();
      auto* renderer = RendererTestAccess::renderer(item);
      if (renderer && renderer != observedRenderer)
      {
        observedRenderer = renderer;
        QObject::connect(renderer, &PlayerRenderer::contextInitialized, window.get(), [&] { ++metrics.contextsCreated; }, Qt::DirectConnection);
        QObject::connect(renderer, &PlayerRenderer::contextInvalidated, window.get(), [&] {
          ++metrics.contextsInvalidated; observedRenderer = nullptr; previousFbo = 0;
        }, Qt::DirectConnection);
      }
    }, Qt::DirectConnection);
    QObject::connect(window.get(), &QQuickWindow::beforeRendering, window.get(), [&] {
      if (!metrics.closing) renderStarted = nowNs();
    }, Qt::DirectConnection);
    QObject::connect(window.get(), &QQuickWindow::afterRendering, window.get(), [&] {
      if (metrics.closing) return;
      ++metrics.renderPasses;
      if (renderStarted) maximum(metrics.renderMaximumNs, nowNs() - renderStarted);
      auto* context = QOpenGLContext::currentContext();
      if (!context) return;
      auto* gl = context->functions();
      if (metrics.glRenderer.isEmpty())
      {
        std::lock_guard<std::mutex> lock(metrics.informationMutex);
        metrics.glRenderer = QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)));
        metrics.glVersion = QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(GL_VERSION)));
        const auto name = metrics.glRenderer.toLower();
        metrics.hardware = !name.contains("llvmpipe") && !name.contains("soft") && !name.contains("gdi generic");
      }
      const auto* fbo = RendererTestAccess::fbo(item);
      if (fbo && fbo->handle() != previousFbo && !RendererTestAccess::visible(item)) ++metrics.hiddenFboAllocations;
      if (RendererTestAccess::output(item) && !RendererTestAccess::visible(item)) ++metrics.hiddenOutputFrames;
      if (fbo && fbo->isValid() && RendererTestAccess::output(item))
      {
        ++metrics.outputFrames;
        if (previousFbo != fbo->handle()) { previousFbo = fbo->handle(); ++metrics.fboChanges; }
        if (metrics.swaps > 10 && nowNs() - lastPixels > 1000000000)
        {
          lastPixels = nowNs();
          GLint previous = 0; gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
          gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle());
          GLubyte upper[4]{}, lower[4]{};
          gl->glReadPixels(fbo->width()/2, 3*fbo->height()/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, upper);
          gl->glReadPixels(fbo->width()/2, fbo->height()/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, lower);
          gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
          ++metrics.pixelSamples;
          if (!(upper[0] > 180 && upper[1] < 70 && upper[2] < 70 && lower[0] < 70 && lower[1] > 180 && lower[2] > 180))
            ++metrics.pixelFailures;
        }
      }
      if (gl->glGetError() != GL_NO_ERROR) ++metrics.glErrors;
    }, Qt::DirectConnection);
    QObject::connect(window.get(), &QQuickWindow::frameSwapped, &app, [&] { ++metrics.swaps; }, Qt::DirectConnection);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
Item { width: 1280; height: 720
  Rectangle { x: 24; y: 24; width: 60; height: 30; color: "blue" }
})", QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/ThreadedOverlay.qml"));
    QElapsedTimer overlayLoading; overlayLoading.start();
    while (component.isLoading() && overlayLoading.elapsed() < 3000)
    { QCoreApplication::processEvents(); QThread::msleep(5); }
    std::unique_ptr<QObject> overlay(component.create());
    if (auto* visual = qobject_cast<QQuickItem*>(overlay.get())) visual->setParentItem(window->contentItem());
    window->show();
    qint64 waitingForLoad = 0, loadRequestedNs = 0;
    qint64 lastHeartbeat = nowNs();
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer); heartbeat.setInterval(100);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
      const auto now = nowNs(); maximum(metrics.guiHeartbeatMaximumNs, now - lastHeartbeat); lastHeartbeat = now;
      metrics.exposed = window && window->isExposed();
      if (hiddenStartup && waitingForLoad && metrics.loadedFiles >= waitingForLoad &&
          metrics.freshVoGeneration == waitingForLoad && metrics.freshPositionGeneration == waitingForLoad)
      {
        item->setVideoVisible(true);
        maximum(metrics.showDelayMaximumNs, nowNs() - loadRequestedNs);
        ++metrics.shownLoads; waitingForLoad = 0;
      }
      if (metrics.guardExceeded) app.quit();
    });
    heartbeat.start();
    QTimer property;
    property.setInterval(250);
    QObject::connect(&property, &QTimer::timeout, &app, [&] {
      if (metrics.stopRequested || !window) return;
      const auto begin = nowNs();
      item->setVideoRectangle({}); item->setPlaybackActive(!hiddenStartup);
      maximum(metrics.guiPropertyMaximumNs, nowNs() - begin);
      if (guiSync)
      {
        metrics.guiSyncInProgress = true;
        const auto syncBegin = nowNs();
        double position = 0;
        mpv_get_property(controller.handle(), "time-pos", MPV_FORMAT_DOUBLE, &position);
        maximum(metrics.guiSyncMaximumNs, nowNs() - syncBegin);
        ++metrics.guiSyncCalls; metrics.guiSyncInProgress = false;
      }
    });
    property.start();
    QTimer windowPixels;
    windowPixels.setInterval(3000);
    QObject::connect(&windowPixels, &QTimer::timeout, &app, [&] {
      if (!captureWindow || metrics.stopRequested || waitingForLoad || !window || !window->isExposed()) return;
      // Public Qt capture of the actual exposed QQuickWindow, including QML.
      // It may request an additional render; its overhead is measured separately.
      const auto begin = nowNs();
      const QImage image = window->grabWindow();
      maximum(metrics.windowGrabMaximumNs, nowNs() - begin);
      ++metrics.windowPixelSamples;
      const bool valid = !image.isNull() && image.width() > 0 && image.height() > 0;
      bool colors = false;
      if (valid)
      {
        const auto upper = image.pixelColor(image.width()/2, image.height()/4);
        const auto lower = image.pixelColor(image.width()/2, 3*image.height()/4);
        const auto blue = image.pixelColor(40*image.width()/window->width(), 40*image.height()/window->height());
        colors = upper.red() > 180 && upper.green() < 70 && upper.blue() < 70 &&
          lower.red() < 70 && lower.green() > 180 && lower.blue() > 180 && blue.blue() > 200 && blue.red() < 50 && blue.green() < 50;
        if (metrics.windowPixelSamples == 1) image.save(outputPath + ".first-window.png");
        image.save(outputPath + ".last-window.png");
      }
      if (!colors) ++metrics.windowPixelFailures;
    });
    if (captureWindow) windowPixels.start();
    auto load = [&] {
      if (hiddenStartup)
      {
        item->setVideoVisible(false); metrics.voConfigured = false; metrics.position = -1;
        waitingForLoad = metrics.loadedFiles + 1; loadRequestedNs = nowNs();
      }
      controller.enqueue({"loadfile", fixture, "replace", "-1", "rebase-start-time=no"});
    };
    QTimer::singleShot(500, &app, load);
    if (hiddenStartup) QTimer::singleShot(duration*500, &app, load);
    QTimer::singleShot(duration*1000, &app, [&] {
      metrics.stopRequestNs = nowNs(); metrics.stopRequested = true;
      property.stop(); windowPixels.stop(); item->setPlaybackActive(false);
      controller.enqueue({"stop"});
    });
    QTimer closePoll;
    closePoll.setInterval(50);
    QObject::connect(&closePoll, &QTimer::timeout, &app, [&] {
      if (!metrics.stopRequested || !metrics.idle || !window) return;
      closePoll.stop(); heartbeat.stop(); metrics.closing = true;
      auto begin = nowNs(); window->close(); metrics.closeCallNs = nowNs() - begin;
      overlay.reset(); delete item; item = nullptr;
      begin = nowNs(); window.reset(); metrics.destroyWindowNs = nowNs() - begin;
      metrics.windowDestroyed = true;
      controller.shutdown(); metrics.finished = true;
      result = acceptable(metrics, captureWindow, hiddenStartup) ? 0 : 1;
      app.quit();
    });
    closePoll.start();
    app.exec();
    // These operations can expose shutdown stalls; the external runner owns
    // the hard deadline and preserves reporter snapshots if they do not return.
    if (window) { overlay.reset(); delete item; window.reset(); }
    controller.shutdown();
  }
  // Retain one final snapshot before ending the independent writer.
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  stopReporter = true; reporter.join();
  return result;
}
