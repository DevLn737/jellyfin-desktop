#include "PlayerQuickItem.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickRenderControl>
#include <QQuickGraphicsDevice>
#include <QQuickRenderTarget>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QThread>
#include <atomic>
#include <cmath>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>

struct Cleanup
{
  std::function<void()> run;
  ~Cleanup() { run(); }
};

// Read-only access; all calls are made on the harness's scenegraph thread.
class RendererTestAccess
{
public:
  static PlayerRenderer* renderer(PlayerQuickItem* item) { return item->m_renderer; }
  static const QOpenGLFramebufferObject* fbo(PlayerQuickItem* item)
  { return item->m_renderer ? item->m_renderer->m_fbo.get() : nullptr; }
  static bool context(PlayerQuickItem* item)
  { return item->m_renderer && item->m_renderer->m_mpvGL; }
  static void swap(PlayerQuickItem* item)
  { if (item->m_renderer) item->m_renderer->swap(); }
};

struct Snapshot
{
  double position = -1;
  bool paused = false;
  bool seeking = true;
  int restarts = 0;
  QString video;
  QString vo;
  QString error;
};

// The only client-event consumer for this isolated mpv core. No normal libmpv
// commands/properties run on, or hold a lock while waiting for, the GL thread.
class Controller
{
public:
  Controller()
  {
    auto result = m_initialized.get_future();
    m_thread = std::thread([this] { run(); });
    m_handle = result.get(); // No render context exists yet.
  }
  ~Controller() { stop(); }
  mpv::qt::Handle handle() const { return m_handle; }
  void command(std::initializer_list<QString> command)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commands.emplace_back(command);
  }
  Snapshot snapshot()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
  }
  void stop()
  {
    m_handle = {}; // Call only after renderer/item have released their copies.
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
  }
private:
  static QString string(mpv_handle* handle, const char* property)
  {
    char* value = mpv_get_property_string(handle, property);
    const QString result = value ? QString::fromUtf8(value) : QString();
    mpv_free(value);
    return result;
  }
  void run()
  {
    auto handle = mpv::qt::Handle::FromRawHandle(mpv_create());
    if (!handle) { m_initialized.set_value({}); return; }
    for (const auto& option : {std::pair{"config", "no"}, {"idle", "yes"},
         {"pause", "yes"}, {"vo", "libmpv"}, {"ao", "null"}, {"hwdec", "no"},
         {"terminal", "no"}, {"osc", "no"}, {"load-scripts", "no"}})
      if (mpv_set_option_string(handle, option.first, option.second) < 0)
      { m_initialized.set_value({}); return; }
    if (mpv_initialize(handle) < 0) { m_initialized.set_value({}); return; }
    m_initialized.set_value(handle);
    Snapshot state;
    while (!m_stop)
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
        const int error = mpv_command_async(handle, 0, arguments.data());
        if (error < 0) state.error = QString::fromUtf8(mpv_error_string(error));
      }
      const auto* event = mpv_wait_event(handle, 0.01);
      if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) ++state.restarts;
      if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0)
        state.error = QString::fromUtf8(mpv_error_string(event->error));
      if (event->event_id == MPV_EVENT_END_FILE)
      {
        const auto* end = static_cast<mpv_event_end_file*>(event->data);
        if (end->error < 0) state.error = QString::fromUtf8(mpv_error_string(end->error));
      }
      double position = -1;
      if (mpv_get_property(handle, "time-pos", MPV_FORMAT_DOUBLE, &position) >= 0)
        state.position = position;
      int paused = 0, seeking = 1;
      mpv_get_property(handle, "pause", MPV_FORMAT_FLAG, &paused);
      mpv_get_property(handle, "seeking", MPV_FORMAT_FLAG, &seeking);
      state.paused = paused; state.seeking = seeking;
      state.video = string(handle, "vid");
      state.vo = string(handle, "current-vo");
      { std::lock_guard<std::mutex> lock(m_mutex); m_snapshot = state; }
    }
    // Last handle reference terminates the isolated core on this worker.
  }
  std::promise<mpv::qt::Handle> m_initialized;
  mpv::qt::Handle m_handle;
  std::thread m_thread;
  std::atomic<bool> m_stop {false};
  std::mutex m_mutex;
  std::deque<QList<QString>> m_commands;
  Snapshot m_snapshot;
};

static void require(bool condition, const QString& message)
{
  if (!condition) throw std::runtime_error(message.toStdString());
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  const auto arguments = app.arguments();
  QJsonObject report {{"schemaVersion", 1}, {"passed", false}, {"qtVersion", qVersion()},
                      {"applicationLaunched", false}, {"windowShown", false}};
  QJsonArray checks;
  int exitCode = 1;
  try
  {
    require(arguments.size() >= 3 && QFile::exists(arguments[1]), "Missing HLS fixture/report arguments");
    QFile fixtureInfo(QFileInfo(arguments[1]).absolutePath() + "/fixture-info.json");
    require(fixtureInfo.open(QIODevice::ReadOnly), "Fixture provenance unavailable");
    const auto fixtureBytes = fixtureInfo.readAll();
    const auto fixture = QJsonDocument::fromJson(fixtureBytes).object();
    const auto hashes = fixture["sha256"].toObject();
    for (const auto& name : {"index.m3u8", "segment-000.ts", "segment-001.ts", "segment-002.ts"})
    {
      QFile asset(QFileInfo(arguments[1]).absolutePath() + "/" + name);
      require(asset.open(QIODevice::ReadOnly), "Fixture asset unavailable: " + QString(name));
      const QString hash = QString::fromLatin1(QCryptographicHash::hash(asset.readAll(), QCryptographicHash::Sha256).toHex());
      require(hash == hashes[name].toString(), "Fixture SHA256 mismatch: " + QString(name));
    }
    report["fixtureManifestSha256"] = QString::fromLatin1(QCryptographicHash::hash(fixtureBytes, QCryptographicHash::Sha256).toHex());
    checks.append(QJsonObject {{"name", "locked-fixture-sha256"}, {"passed", true}});
    Controller controller;
    require(bool(controller.handle()), "Could not initialize isolated mpv core");
    QOpenGLContext context;
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::NoProfile);
    format.setDepthBufferSize(24); format.setStencilBufferSize(8);
    context.setFormat(format);
    require(context.create(), "Could not create software OpenGL context");
    QOffscreenSurface surface;
    surface.setFormat(context.format()); surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "Offscreen surface/context unavailable");
    auto* gl = context.functions();
    report["glRenderer"] = QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)));
    report["glVersion"] = QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(GL_VERSION)));
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setGeometry(0, 0, 256, 144); window.setColor(QColor(0, 0, 0));
    window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
    require(control.initialize(), "RenderControl initialization failed");
    QOpenGLFramebufferObject target(QSize(256, 144));
    require(target.isValid(), "Output texture FBO invalid");
    auto renderTarget = QQuickRenderTarget::fromOpenGLTexture(target.texture(), target.size());
    renderTarget.setDevicePixelRatio(1);
    window.setRenderTarget(renderTarget);
    // Match QML construction: attach after the derived constructor connects
    // windowChanged, rather than during QQuickItem's base constructor.
    auto* item = new PlayerQuickItem;
    item->setParentItem(window.contentItem());
    QString rendererError;
    int invalidations = 0;
    Cleanup cleanup {[&] {
      if (item)
      {
        context.makeCurrent(&surface);
        control.invalidate();
        delete item; item = nullptr;
        window.setRenderTarget({});
      }
    }};
    item->setSize(window.size());
    require(item->initMpvHandle(controller.handle()), "Native handle injection failed");
    item->setVideoVisible(true); item->setVideoRectangle(QRect(16, 20, 128, 72));
    QObject::disconnect(item, &PlayerQuickItem::onFatalError, item, nullptr);
    QObject::connect(item, &PlayerQuickItem::onFatalError, &app,
      [&](const QString& error) { rendererError = error; });
    QObject::connect(item, &PlayerQuickItem::renderContextInvalidated, &app,
      [&] { ++invalidations; }, Qt::DirectConnection);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
Item {
  width: 256; height: 144
  Rectangle { x: 20; y: 24; width: 12; height: 12; color: "blue" }
  Rectangle { x: 4; y: 128; width: 12; height: 12; color: "lime" }
})", QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/RendererTest.qml"));
    QElapsedTimer componentTimer; componentTimer.start();
    while (component.isLoading() && componentTimer.elapsed() < 5000)
    { QCoreApplication::processEvents(); QThread::msleep(5); }
    std::unique_ptr<QObject> overlay(component.create());
    require(bool(overlay), "Qt overlay component unavailable: " + component.errorString());
    qobject_cast<QQuickItem*>(overlay.get())->setParentItem(window.contentItem());
    QImage image;
    auto render = [&] {
      require(!window.isVisible() && !window.isExposed(), "Offscreen window unexpectedly exposed");
      require(context.makeCurrent(&surface), "makeCurrent failed");
      control.polishItems(); control.beginFrame(); control.sync(); control.render(); control.endFrame();
      // RenderControl has no presentation swap; report the completed frame once.
      require(context.makeCurrent(&surface), "Context lost after frame");
      gl->glFinish(); RendererTestAccess::swap(item);
      image = target.toImage();
      require(rendererError.isEmpty(), rendererError);
      require(gl->glGetError() == GL_NO_ERROR, "OpenGL error after production render/readback");
    };
    auto pixels = [&] {
      const auto red = image.pixelColor(70, 40), cyan = image.pixelColor(70, 75);
      const auto blue = image.pixelColor(24, 28);
      const auto green = image.pixelColor(8, 132), clear = image.pixelColor(200, 40);
      return red.red() > 200 && red.green() < 40 && red.blue() < 40
        && cyan.red() < 40 && cyan.green() > 200 && cyan.blue() > 200
        && blue.blue() > 230 && blue.red() < 20 && blue.green() < 20
        && green.green() > 230 && green.red() < 20 && green.blue() < 20
        && clear.red() < 20 && clear.green() < 20 && clear.blue() < 20;
    };
    auto wait = [&](const QString& label, auto condition) {
      QElapsedTimer timer; timer.start();
      while (timer.elapsed() < 10000)
      {
        QCoreApplication::processEvents(); render();
        const auto state = controller.snapshot();
        require(state.error.isEmpty(), label + ": " + state.error);
        if (condition(state))
        {
          checks.append(QJsonObject {{"name", label}, {"passed", true},
            {"timePos", state.position}, {"vid", state.video}, {"vo", state.vo},
            {"restarts", state.restarts}, {"durationMs", int(timer.elapsed())}});
          std::cout << label.toStdString() << " position=" << state.position
                    << " vo=" << state.vo.toStdString() << std::endl;
          return;
        }
        QThread::msleep(5);
      }
      const auto state = controller.snapshot();
      image.save(arguments[2] + ".failure.png");
      throw std::runtime_error(QString("%1 timeout: pos=%2 pause=%3 seeking=%4 vid=%5 vo=%6 restarts=%7 red=%8")
        .arg(label).arg(state.position).arg(state.paused).arg(state.seeking)
        .arg(state.video, state.vo).arg(state.restarts).arg(pixels()).toStdString());
    };
    // Initialize the production render context before opening the video.
    render(); require(RendererTestAccess::context(item), "Production libmpv render context missing");
    const auto* initializedRenderer = RendererTestAccess::renderer(item);
    require(!item->initMpvHandle({}) && item->initMpvHandle(controller.handle())
      && RendererTestAccess::renderer(item) == initializedRenderer && RendererTestAccess::context(item),
      "Native handle seam changed the live renderer");
    controller.command({"loadfile", arguments[1], "replace", "-1", "rebase-start-time=no,start=30"});
    wait("paused-original-load-30-and-composite", [&](const Snapshot& s) {
      return s.restarts > 0 && s.paused && !s.seeking && std::abs(s.position - 30) < 0.2
        && s.video != "no" && !s.video.isEmpty() && s.vo == "libmpv" && pixels();
    });
    const int restarts = controller.snapshot().restarts;
    controller.command({"seek", "32", "absolute+exact"});
    wait("paused-original-seek-32-and-composite", [&](const Snapshot& s) {
      return s.restarts > restarts && s.paused && !s.seeking && std::abs(s.position - 32) < 0.2 && pixels();
    });
    const auto* initialFbo = RendererTestAccess::fbo(item);
    const GLuint initialHandle = initialFbo->handle();
    require(initialFbo->size() == QSize(128, 72), "Initial video FBO has wrong dimensions");
    for (int frame = 0; frame < 12; ++frame)
    {
      render();
      require(RendererTestAccess::fbo(item) == initialFbo
        && RendererTestAccess::fbo(item)->handle() == initialHandle && pixels(), "Paused frame changed reusable FBO/composite");
    }
    checks.append(QJsonObject {{"name", "paused-fbo-reuse-12-frames"}, {"passed", true}, {"fbo", int(initialHandle)}});
    item->setVideoRectangle(QRect(40, 60, 160, 80)); render();
    require(RendererTestAccess::fbo(item)->size() == QSize(160, 80), "Resized FBO dimensions wrong");
    const auto resizedRed = image.pixelColor(120, 80), resizedCyan = image.pixelColor(120, 120);
    const auto oldClear = image.pixelColor(70, 40);
    require(resizedRed.red() > 200 && resizedRed.green() < 40
      && resizedCyan.red() < 40 && resizedCyan.green() > 200 && resizedCyan.blue() > 200 && oldClear.red() < 20,
      "Video rectangle resize/clear/orientation failed");
    const auto* resizedFbo = RendererTestAccess::fbo(item);
    const GLuint resizedHandle = resizedFbo->handle();
    render(); require(RendererTestAccess::fbo(item) == resizedFbo
      && RendererTestAccess::fbo(item)->handle() == resizedHandle, "Resized FBO is not reused");
    checks.append(QJsonObject {{"name", "rectangle-resize-and-reuse"}, {"passed", true}});
    require(context.makeCurrent(&surface), "makeCurrent before invalidation failed");
    control.invalidate();
    require(!RendererTestAccess::renderer(item) && invalidations == 1, "Invalidation failed to destroy renderer/context");
    QCoreApplication::processEvents();
    checks.append(QJsonObject {{"name", "safe-invalidation"}, {"passed", true}});
    // Recovery policy belongs to PlayerComponent; this fixture deliberately
    // tests resource cleanup without manufacturing a production restore pass.
    overlay.reset(); delete item; item = nullptr;
    window.setRenderTarget({});
    controller.stop();
    report["passed"] = true; exitCode = 0;
  }
  catch (const std::exception& error)
  {
    report["error"] = QString::fromUtf8(error.what());
    std::cerr << error.what() << std::endl;
  }
  report["checks"] = checks;
  if (argc >= 3)
  {
    QFile output(arguments[2]);
    if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(report).toJson());
  }
  return exitCode;
}
