#ifndef PLAYERQUICKITEM_H
#define PLAYERQUICKITEM_H

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QPointer>
#include <memory>
#include <mpv/render.h>
#include "QtHelper.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
using HANDLE = void*;
#endif

class PlayerComponent;
class RepaintDispatcher;
class RendererTestAccess;

// All OpenGL and mpv render-context operations belong to the scenegraph thread.
// PlayerComponent remains the only consumer of mpv client events.
class PlayerRenderer : public QObject
{
  Q_OBJECT
  friend class PlayerQuickItem;
  friend class RendererTestAccess;
public:
  PlayerRenderer(mpv::qt::Handle mpv, QQuickWindow* window,
                 std::shared_ptr<RepaintDispatcher> repaint);
  ~PlayerRenderer() override;
  void prepare();
  void composite();
  void swap();

signals:
  void fatalError(QString message);
  void contextInitialized();
  void contextInvalidated();

private:
  bool init();
  void setPlaybackActive(bool active);
  static void onUpdate(void* ctx);
  mpv::qt::Handle m_mpv;
  mpv_render_context* m_mpvGL = nullptr;
  QQuickWindow* m_window;
  std::shared_ptr<RepaintDispatcher> m_repaint;
  std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
  QOpenGLShaderProgram m_program;
  QOpenGLBuffer m_vertices {QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject m_vao;
  QSize m_size;
  QRect m_videoRectangle;
  HANDLE m_hAvrtHandle = nullptr;
  bool m_visible = false;
  bool m_playbackActive = false;
  bool m_failed = false;
  bool m_hasFrame = false;
  bool m_hiddenFrameRendered = false;
};

class PlayerQuickItem : public QQuickItem
{
  Q_OBJECT
public:
  explicit PlayerQuickItem(QQuickItem* parent = nullptr);
  ~PlayerQuickItem() override;
  void initMpv(PlayerComponent* player);
  // Native embedding seam. Client events are consumed by the handle's owner.
  // A live item owns one stable handle; replacing it requires a new item.
  bool initMpvHandle(mpv::qt::Handle handle);
  void setVideoRectangle(const QRect& rectangle);
  void setVideoVisible(bool visible);
  void setPlaybackActive(bool active);
  QString debugInfo() const { return m_debugInfo; }
  void releaseResources() override;

signals:
  void onFatalError(QString message);
  void renderContextRecreated();
  // Emitted on the render thread before free; receivers may only set an
  // atomic lifecycle marker here. Restore GUI/mpv state on recreated instead.
  void renderContextInvalidated();

private slots:
  void onWindowChanged(QQuickWindow* win);
  void onSynchronize();
  void onInvalidate();
  void onHandleFatalError(QString message);

private:
  friend class RendererTestAccess;
  mpv::qt::Handle m_mpv;
  PlayerRenderer* m_renderer = nullptr;
  QPointer<QQuickWindow> m_renderWindow;
  std::shared_ptr<RepaintDispatcher> m_repaint;
  QString m_debugInfo;
  QRect m_videoRectangle;
  bool m_videoVisible = false;
  bool m_playbackActive = false;
  bool m_destroying = false;
  bool m_renderContextWasInitialized = false;
};
#endif
