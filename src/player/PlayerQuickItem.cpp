#include "PlayerQuickItem.h"
#include "profiling/Trace.h"
#include "utils/Utils.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>
#include <QRunnable>
#include <QThread>
#include <atomic>
#include <mpv/render_gl.h>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <avrt.h>
#endif

// The libmpv callback only queues a coalesced GUI update. This object is held
// until the render context has detached the callback and completed destruction.
class RepaintDispatcher : public QObject
{
public:
  explicit RepaintDispatcher(QQuickWindow* window) : m_window(window) {}
  void request()
  {
    if (!m_active.load() || m_pending.exchange(true))
      return;
    QMetaObject::invokeMethod(this, [this] {
      m_pending.store(false);
      if (m_active.load() && m_window)
        m_window->update();
    }, Qt::QueuedConnection);
  }
  void stop() { m_active.store(false); }
private:
  QPointer<QQuickWindow> m_window; // Only accessed on the GUI thread.
  std::atomic<bool> m_active {true};
  std::atomic<bool> m_pending {false};
};

namespace {
void* getProcAddress(void*, const char* name)
{
  auto* context = QOpenGLContext::currentContext();
  if (!context)
    return nullptr;
  void* result = reinterpret_cast<void*>(context->getProcAddress(QByteArray(name)));
#ifdef Q_OS_WIN
  if (!result)
    if (auto module = GetModuleHandleW(L"opengl32.dll"))
      result = reinterpret_cast<void*>(GetProcAddress(module, name));
#endif
  return result;
}

// Qt does not promise framebuffer or viewport restoration in resetOpenGLState.
class ExternalCommands
{
public:
  explicit ExternalCommands(QQuickWindow* window) : m_window(window)
  {
    m_window->beginExternalCommands();
    m_gl = QOpenGLContext::currentContext()->functions();
    m_gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_framebuffer);
    m_gl->glGetIntegerv(GL_VIEWPORT, m_viewport);
    QQuickOpenGLUtils::resetOpenGLState();
    // resetOpenGLState binds the default framebuffer. The current Qt render
    // target may be an offscreen framebuffer, so restore it before drawing.
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(m_framebuffer));
    m_gl->glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
  }
  ~ExternalCommands()
  {
    QQuickOpenGLUtils::resetOpenGLState();
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(m_framebuffer));
    m_gl->glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    m_window->endExternalCommands();
  }
private:
  QQuickWindow* m_window;
  QOpenGLFunctions* m_gl;
  GLint m_framebuffer = 0;
  GLint m_viewport[4] {};
};

class CleanupRenderer : public QRunnable
{
public:
  explicit CleanupRenderer(PlayerRenderer* renderer) : m_renderer(renderer) {}
  void run() override { delete m_renderer; }
private:
  PlayerRenderer* m_renderer;
};
}

PlayerRenderer::PlayerRenderer(mpv::qt::Handle mpv, QQuickWindow* window,
                               std::shared_ptr<RepaintDispatcher> repaint)
  : m_mpv(mpv), m_window(window), m_repaint(std::move(repaint))
{
}

bool PlayerRenderer::init()
{
  mpv_opengl_init_params glParams {getProcAddress, nullptr};
  mpv_render_param params[] {
    {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
    {MPV_RENDER_PARAM_INVALID, nullptr}
  };
  if (mpv_render_context_create(&m_mpvGL, m_mpv, params) < 0)
    return false;

  auto* context = QOpenGLContext::currentContext();
  const bool core = context->format().profile() == QSurfaceFormat::CoreProfile;
  const QByteArray vertex = core
    ? "#version 150\nin vec2 position; in vec2 uv; out vec2 texCoord; uniform vec4 rect; void main() { texCoord=uv; gl_Position=vec4(rect.xy+position*rect.zw,0.0,1.0); }"
    : "attribute vec2 position; attribute vec2 uv; varying vec2 texCoord; uniform vec4 rect; void main() { texCoord=uv; gl_Position=vec4(rect.xy+position*rect.zw,0.0,1.0); }";
  const QByteArray fragment = core
    ? "#version 150\nin vec2 texCoord; out vec4 fragColor; uniform sampler2D video; void main() { fragColor=texture(video,texCoord); }"
    : "varying vec2 texCoord; uniform sampler2D video; void main() { gl_FragColor=texture2D(video,texCoord); }";
  if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
      || !m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment))
    return false;
  m_program.bindAttributeLocation("position", 0);
  m_program.bindAttributeLocation("uv", 1);
  if (!m_program.link() || !m_vertices.create())
    return false;
  // A VAO is required by core-profile drivers; it is optional with GL 2.1.
  if (!m_vao.create() && core)
    return false;
  const GLfloat vertices[] {0,0, 0,0, 1,0, 1,0, 0,1, 0,1, 1,1, 1,1};
  m_vertices.bind();
  m_vertices.allocate(vertices, int(sizeof(vertices)));
  m_vertices.release();
  mpv_render_context_set_update_callback(m_mpvGL, onUpdate, m_repaint.get());
#ifdef Q_OS_WIN
  DwmEnableMMCSS(TRUE);
#endif
  emit contextInitialized();
  return true;
}

PlayerRenderer::~PlayerRenderer()
{
  if (m_mpvGL)
  {
    emit contextInvalidated();
    mpv_render_context_set_update_callback(m_mpvGL, nullptr, nullptr);
    mpv_render_context_free(m_mpvGL);
    m_mpvGL = nullptr;
  }
  // These resources are destroyed while the scenegraph GL context is current.
  m_fbo.reset();
  m_vertices.destroy();
  m_vao.destroy();
  m_program.removeAllShaders();
  setPlaybackActive(false);
}

void PlayerRenderer::prepare()
{
  JMP_PROFILE_ZONE("Renderer prepare mpv frame");
  if (m_failed || m_size.isEmpty() || !QOpenGLContext::currentContext())
    return;
  ExternalCommands commands(m_window);
  if (!m_mpvGL && !init())
  {
    m_failed = true;
    emit fatalError(tr("Could not initialize the OpenGL video renderer."));
    return;
  }
  // Consume notifications even before vo-configured makes the item visible.
  // mpv may be waiting for that first frame while the GUI has not yet received
  // its visibility event. Leaving it queued here delays startup until timeout.
  const auto updates = mpv_render_context_update(m_mpvGL);
  setPlaybackActive(m_playbackActive);
  if (!m_visible)
  {
    m_hasFrame = false;
    if (updates & MPV_RENDER_UPDATE_FRAME)
    {
      int skip = 1;
      int block = 0;
      mpv_render_param params[] {
        {MPV_RENDER_PARAM_SKIP_RENDERING, &skip},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
        {MPV_RENDER_PARAM_INVALID, nullptr}
      };
      // A skipped frame still needs a swap acknowledgement. Visible rendering
      // retains mpv's normal audio/video timing and redraws paused frames.
      m_hiddenFrameRendered = mpv_render_context_render(m_mpvGL, params) >= 0;
    }
    return;
  }

  auto* gl = QOpenGLContext::currentContext()->functions();
  const QSize targetSize = m_videoRectangle.size();
  GLint maxTextureSize = 0;
  gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  if (targetSize.isEmpty() || targetSize.width() > maxTextureSize || targetSize.height() > maxTextureSize)
  {
    m_hasFrame = false;
    return;
  }
  if (!m_fbo || m_fbo->size() != targetSize)
  {
    m_hasFrame = false;
    QOpenGLFramebufferObjectFormat format;
    format.setInternalTextureFormat(GL_RGBA8);
    m_fbo = std::make_unique<QOpenGLFramebufferObject>(targetSize, format);
  }
  if (!m_fbo->isValid())
    return;

  m_fbo->bind();
  gl->glViewport(0, 0, targetSize.width(), targetSize.height());
  gl->glDisable(GL_SCISSOR_TEST);
  gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  gl->glClearColor(0, 0, 0, 1);
  gl->glClear(GL_COLOR_BUFFER_BIT);
  mpv_opengl_fbo fbo {int(m_fbo->handle()), targetSize.width(), targetSize.height(), GL_RGBA8};
  // mpv's image coordinates start at the top. Flip into the FBO so its
  // texture can be composited with ordinary bottom-left OpenGL UVs.
  int flip = 1;
  mpv_render_param params[] {
    {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
    {MPV_RENDER_PARAM_FLIP_Y, &flip},
    {MPV_RENDER_PARAM_INVALID, nullptr}
  };
  m_hasFrame = mpv_render_context_render(m_mpvGL, params) >= 0;
}

void PlayerRenderer::composite()
{
  JMP_PROFILE_ZONE("Renderer composite video");
  if (!m_visible || !m_hasFrame || !m_fbo || m_size.isEmpty())
    return;
  // beforeRenderPassRecording is after Qt's clear, inside the main render pass.
  // Use its actual framebuffer (including Qt-owned/offscreen targets), never 0.
  ExternalCommands commands(m_window);
  auto* gl = QOpenGLContext::currentContext()->functions();
  gl->glViewport(0, 0, m_size.width(), m_size.height());
  gl->glDisable(GL_DEPTH_TEST);
  gl->glDisable(GL_STENCIL_TEST);
  gl->glDisable(GL_SCISSOR_TEST);
  gl->glDisable(GL_BLEND);
  gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  m_program.bind();
  m_program.setUniformValue("video", 0);
  m_program.setUniformValue("rect", QVector4D(
    2.0f * m_videoRectangle.x() / m_size.width() - 1.0f,
    1.0f - 2.0f * (m_videoRectangle.y() + m_videoRectangle.height()) / m_size.height(),
    2.0f * m_videoRectangle.width() / m_size.width(),
    2.0f * m_videoRectangle.height() / m_size.height()));
  if (m_vao.isCreated()) m_vao.bind();
  m_vertices.bind();
  m_program.enableAttributeArray(0);
  m_program.enableAttributeArray(1);
  m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * int(sizeof(GLfloat)));
  m_program.setAttributeBuffer(1, GL_FLOAT, 2 * int(sizeof(GLfloat)), 2, 4 * int(sizeof(GLfloat)));
  gl->glActiveTexture(GL_TEXTURE0);
  gl->glBindTexture(GL_TEXTURE_2D, m_fbo->texture());
  gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  m_program.disableAttributeArray(0);
  m_program.disableAttributeArray(1);
  m_vertices.release();
  if (m_vao.isCreated()) m_vao.release();
  m_program.release();
}

void PlayerRenderer::swap()
{
  JMP_PROFILE_ZONE("Renderer frame swapped");
  JMP_PROFILE_FRAME();
  if (m_mpvGL && (m_hasFrame || m_hiddenFrameRendered))
  {
    mpv_render_context_report_swap(m_mpvGL);
    m_hiddenFrameRendered = false;
  }
}

void PlayerRenderer::setPlaybackActive(bool active)
{
#ifdef Q_OS_WIN
  if (active && !m_hAvrtHandle)
  {
    DWORD taskIndex = 0;
    m_hAvrtHandle = AvSetMmThreadCharacteristicsW(L"Low Latency", &taskIndex);
  }
  else if (!active && m_hAvrtHandle)
  {
    AvRevertMmThreadCharacteristics(m_hAvrtHandle);
    m_hAvrtHandle = nullptr;
  }
#else
  Q_UNUSED(active);
#endif
}

void PlayerRenderer::onUpdate(void* ctx)
{
  static_cast<RepaintDispatcher*>(ctx)->request();
}

PlayerQuickItem::PlayerQuickItem(QQuickItem* parent) : QQuickItem(parent)
{
  connect(this, &QQuickItem::windowChanged, this, &PlayerQuickItem::onWindowChanged);
  connect(this, &PlayerQuickItem::onFatalError, this, &PlayerQuickItem::onHandleFatalError, Qt::QueuedConnection);
  if (window()) onWindowChanged(window());
}

PlayerQuickItem::~PlayerQuickItem()
{
  m_destroying = true;
  releaseResources();
}

void PlayerQuickItem::onWindowChanged(QQuickWindow* win)
{
  if (m_renderWindow && m_renderWindow != win)
  {
    releaseResources();
    disconnect(m_renderWindow, nullptr, this, nullptr);
  }
  m_renderWindow = win;
  if (!win)
    return;
  if (m_mpv)
  {
    if (m_repaint) m_repaint->stop();
    m_repaint = std::shared_ptr<RepaintDispatcher>(new RepaintDispatcher(win),
      [](RepaintDispatcher* dispatcher) { dispatcher->deleteLater(); });
  }
  // Preserve the active libmpv render context over ordinary minimize/restore.
  // Freeing it during playback deselects video. Invalidation and explicit
  // release still destroy all GL resources on the scenegraph thread.
  win->setPersistentGraphics(true);
  win->setPersistentSceneGraph(true);
  connect(win, &QQuickWindow::beforeSynchronizing, this, &PlayerQuickItem::onSynchronize, Qt::DirectConnection);
  connect(win, &QQuickWindow::sceneGraphInvalidated, this, &PlayerQuickItem::onInvalidate, Qt::DirectConnection);
}

void PlayerQuickItem::onHandleFatalError(QString message)
{
  throw FatalException(message);
}

void PlayerQuickItem::onSynchronize()
{
  // The GUI thread is blocked during this signal. Copy geometry/visibility here.
  if (!m_renderer && m_mpv)
  {
    if (!m_repaint)
      return;
    m_renderer = new PlayerRenderer(m_mpv, window(), m_repaint);
    connect(m_renderer, &PlayerRenderer::fatalError, this, &PlayerQuickItem::onFatalError, Qt::QueuedConnection);
    connect(m_renderer, &PlayerRenderer::contextInvalidated, this,
            &PlayerQuickItem::renderContextInvalidated, Qt::DirectConnection);
    connect(m_renderer, &PlayerRenderer::contextInitialized, this, [this] {
      if (m_renderContextWasInitialized)
        emit renderContextRecreated();
      m_renderContextWasInitialized = true;
    }, Qt::QueuedConnection);
    connect(window(), &QQuickWindow::beforeRendering, m_renderer, &PlayerRenderer::prepare, Qt::DirectConnection);
    connect(window(), &QQuickWindow::beforeRenderPassRecording, m_renderer, &PlayerRenderer::composite, Qt::DirectConnection);
    connect(window(), &QQuickWindow::frameSwapped, m_renderer, &PlayerRenderer::swap, Qt::DirectConnection);
    auto* context = QOpenGLContext::currentContext();
    m_debugInfo.clear();
    if (context && context->isValid())
    {
      m_debugInfo = "\nOpenGL:\n";
      for (const GLenum name : {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION})
        if (const auto* value = context->functions()->glGetString(name))
          m_debugInfo += "  " + QString::fromUtf8(reinterpret_cast<const char*>(value)) + "\n";
    }
  }
  if (m_renderer)
  {
    m_renderer->m_size = window()->size() * window()->effectiveDevicePixelRatio();
    const QRect rectangle = m_videoRectangle;
    m_renderer->m_videoRectangle = rectangle.width() > 0 && rectangle.height() > 0
      ? rectangle : QRect(QPoint(0, 0), m_renderer->m_size);
    m_renderer->m_visible = m_videoVisible;
    m_renderer->m_playbackActive = m_playbackActive;
  }
}

void PlayerQuickItem::onInvalidate()
{
  // Qt calls this on the render thread with its GL context current.
  delete m_renderer;
  m_renderer = nullptr;
  // Keep the GUI dispatcher for a recreated scenegraph. The old context has
  // detached its callback before freeing its GL resources.
}

void PlayerQuickItem::releaseResources()
{
  if (m_repaint)
    m_repaint->stop();
  if (m_renderer && m_renderWindow)
  {
    auto* renderer = m_renderer;
    m_renderer = nullptr;
    // Disconnect paint signals before the deferred render-thread destruction.
    disconnect(m_renderWindow, nullptr, renderer, nullptr);
    m_renderWindow->scheduleRenderJob(new CleanupRenderer(renderer), QQuickWindow::BeforeSynchronizingStage);
  }
  m_repaint.reset();
  if (!m_destroying && window() && m_mpv)
    m_repaint = std::shared_ptr<RepaintDispatcher>(new RepaintDispatcher(window()),
      [](RepaintDispatcher* dispatcher) { dispatcher->deleteLater(); });
}

bool PlayerQuickItem::initMpvHandle(mpv::qt::Handle handle)
{
  if (!handle) return false;
  if (m_mpv)
    return static_cast<mpv_handle*>(m_mpv) == static_cast<mpv_handle*>(handle);
  m_mpv = std::move(handle);
  if (window())
    m_repaint = std::shared_ptr<RepaintDispatcher>(new RepaintDispatcher(window()),
      [](RepaintDispatcher* dispatcher) { dispatcher->deleteLater(); });
  if (window()) window()->update();
  return true;
}

void PlayerQuickItem::setVideoRectangle(const QRect& rectangle)
{
  m_videoRectangle = rectangle;
  if (window()) window()->update();
}

void PlayerQuickItem::setVideoVisible(bool visible)
{
  m_videoVisible = visible;
  setVisible(visible);
  if (window()) window()->update();
}

void PlayerQuickItem::setPlaybackActive(bool active)
{
  m_playbackActive = active;
  if (window()) window()->update();
}
