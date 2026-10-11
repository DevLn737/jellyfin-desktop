#ifndef TASKBARCOMPONENTWIN_H
#define TASKBARCOMPONENTWIN_H

#include <QAbstractNativeEventFilter>
#include <shobjidl.h>

#include <wrl.h>
#include <Windows.Media.h>

#include "TaskbarComponent.h"

class TaskbarComponentWin : public TaskbarComponent, public QAbstractNativeEventFilter
{
public:
  TaskbarComponentWin(): TaskbarComponent(nullptr) {}
  ~TaskbarComponentWin() override;
  virtual void setWindow(QQuickWindow* window) override;
  bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
  void onPauseClicked();
  void onPrevClicked();
  void onNextClicked();
  void setProgress(quint64 value);
  void setControlsVisible(bool value);
  void setPaused(bool value);
  void initializeMediaTransport(HWND hwnd);
  void initializeTaskbar();
  void updateTaskbarButtons();
  void sendMediaAction(const QString& action);
  void onMetaData(const QVariantMap &meta, QUrl baseUrl);
  void setAudioMeta(const QVariantMap &meta);
  void setVideoMeta(const QVariantMap &meta);
  void setThumbnail(const QVariantMap &meta, QUrl baseUrl);
  void playing();
  void stopped();
  void paused();

  HRESULT buttonPressed(ABI::Windows::Media::ISystemMediaTransportControls* sender,
    ABI::Windows::Media::ISystemMediaTransportControlsButtonPressedEventArgs* args);

  HWND m_hwnd = nullptr;
  UINT m_taskbarCreated = 0;
  bool m_taskbarEnabled = false;
  bool m_buttonsAdded = false;
  bool m_controlsVisible = false;
  bool m_paused = false;
  HICON m_icons[4] {};
  Microsoft::WRL::ComPtr<ITaskbarList3> m_taskbar;
  bool m_runtimeInitialized = false;
  bool m_initialized = false;
  bool m_buttonSubscribed = false;
  EventRegistrationToken m_buttonPressedToken {};
  Microsoft::WRL::ComPtr<ABI::Windows::Media::ISystemMediaTransportControls> m_systemControls;
  Microsoft::WRL::ComPtr<ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater> m_displayUpdater;
  Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReference> m_thumbnail;
};

#endif // TASKBARCOMPONENTWIN_H
