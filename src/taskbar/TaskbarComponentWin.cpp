#include <QApplication>
#include <QStyle>
#include <QUrlQuery>
#include <QImage>
#include <QMetaObject>
#include <cstring>
#include <vector>

#include <Windows.Foundation.h>
#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include "TaskbarComponentWin.h"
#include "PlayerComponent.h"
#include "input/InputComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Media;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using ABI::Windows::Storage::Streams::IRandomAccessStreamReference;
using ABI::Windows::Storage::Streams::IRandomAccessStreamReferenceStatics;

namespace {
constexpr UINT PreviousButton = 1;
constexpr UINT PauseButton = 2;
constexpr UINT NextButton = 3;

HICON nativeIcon(QStyle::StandardPixmap standardPixmap)
{
  const QImage image = QApplication::style()->standardIcon(standardPixmap)
    .pixmap(32, 32).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) return nullptr;
  BITMAPINFO info {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = image.width();
  info.bmiHeader.biHeight = -image.height();
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!color) return nullptr;
  std::memcpy(pixels, image.constBits(), size_t(image.sizeInBytes()));
  const std::vector<BYTE> maskBits(size_t((image.width() + 15) / 16 * 2 * image.height()), 0);
  HBITMAP mask = CreateBitmap(image.width(), image.height(), 1, 1, maskBits.data());
  ICONINFO iconInfo {};
  iconInfo.fIcon = TRUE;
  iconInfo.hbmColor = color;
  iconInfo.hbmMask = mask;
  HICON icon = mask ? CreateIconIndirect(&iconInfo) : nullptr;
  if (mask) DeleteObject(mask);
  DeleteObject(color);
  return icon;
}
}

/////////////////////////////////////////////////////////////////////////////////////////
TaskbarComponentWin::~TaskbarComponentWin()
{
  if (QCoreApplication::instance())
    QCoreApplication::instance()->removeNativeEventFilter(this);
  if (m_buttonSubscribed && m_systemControls)
    m_systemControls->remove_ButtonPressed(m_buttonPressedToken);
  if (m_displayUpdater)
    m_displayUpdater->ClearAll();
  if (m_taskbar && m_hwnd)
    m_taskbar->SetProgressState(m_hwnd, TBPF_NOPROGRESS);
  for (HICON icon : m_icons)
    if (icon) DestroyIcon(icon);
  // Keep the apartment alive until every native interface is released. This
  // component is static and can outlive QApplication's own OleInitialize.
  m_thumbnail.Reset();
  m_displayUpdater.Reset();
  m_systemControls.Reset();
  m_taskbar.Reset();
  if (m_runtimeInitialized)
    RoUninitialize();
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setWindow(QQuickWindow* window)
{
  qDebug() << "Taskbar initialization started";
  TaskbarComponent::setWindow(window);

  bool EnableTaskbar = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "enableWindowsTaskbarIntegration").toBool();
  bool EnableMediaControls = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "enableWindowsMediaIntegration").toBool();

  if ((EnableTaskbar || EnableMediaControls) && !m_runtimeInitialized)
  {
    // Match Qt's GUI STA. Never attempt to switch it to an MTA.
    const HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    m_runtimeInitialized = SUCCEEDED(result);
    if (!m_runtimeInitialized)
      qWarning() << "Windows Runtime initialization failed:" << Qt::hex << quint32(result);
  }

  m_hwnd = reinterpret_cast<HWND>(window->winId());
  m_taskbarEnabled = EnableTaskbar;
  if (m_taskbarEnabled) {
    m_taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    m_icons[0] = nativeIcon(QStyle::SP_MediaSkipBackward);
    m_icons[1] = nativeIcon(QStyle::SP_MediaPause);
    m_icons[2] = nativeIcon(QStyle::SP_MediaPlay);
    m_icons[3] = nativeIcon(QStyle::SP_MediaSkipForward);
    QCoreApplication::instance()->installNativeEventFilter(this);
    initializeTaskbar();
  }

  connect(&PlayerComponent::Get(), &PlayerComponent::positionUpdate, this, &TaskbarComponentWin::setProgress);
  connect(&PlayerComponent::Get(), &PlayerComponent::playing, this, &TaskbarComponentWin::playing);
  connect(&PlayerComponent::Get(), &PlayerComponent::paused, this, &TaskbarComponentWin::paused);
  connect(&PlayerComponent::Get(), &PlayerComponent::stopped, this, &TaskbarComponentWin::stopped);
  connect(&PlayerComponent::Get(), &PlayerComponent::onMetaData, this, &TaskbarComponentWin::onMetaData);

  setControlsVisible(false);
  setPaused(false);

  if (EnableMediaControls) {
    initializeMediaTransport(m_hwnd);
  }
}

void TaskbarComponentWin::initializeTaskbar()
{
  if (!m_taskbarEnabled || !m_hwnd) return;
  m_taskbar.Reset();
  m_buttonsAdded = false;
  if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(m_taskbar.GetAddressOf())))
      || FAILED(m_taskbar->HrInit()))
  {
    m_taskbar.Reset();
    qWarning() << "Windows taskbar API initialization failed";
    return;
  }
  updateTaskbarButtons();
}

void TaskbarComponentWin::updateTaskbarButtons()
{
  if (!m_taskbar || !m_hwnd) return;
  THUMBBUTTON buttons[3] {};
  const UINT ids[] {PreviousButton, PauseButton, NextButton};
  HICON icons[] {m_icons[0], m_paused ? m_icons[2] : m_icons[1], m_icons[3]};
  const wchar_t* tips[] {L"Previous", m_paused ? L"Resume" : L"Pause", L"Next"};
  for (int i = 0; i < 3; ++i)
  {
    buttons[i].dwMask = static_cast<THUMBBUTTONMASK>(THB_ICON | THB_TOOLTIP | THB_FLAGS);
    buttons[i].iId = ids[i];
    buttons[i].hIcon = icons[i];
    buttons[i].dwFlags = m_controlsVisible ? THBF_ENABLED : THBF_HIDDEN;
    lstrcpynW(buttons[i].szTip, tips[i], int(sizeof(buttons[i].szTip) / sizeof(wchar_t)));
  }
  if (m_buttonsAdded)
    m_taskbar->ThumbBarUpdateButtons(m_hwnd, 3, buttons);
  else
    m_buttonsAdded = SUCCEEDED(m_taskbar->ThumbBarAddButtons(m_hwnd, 3, buttons));
  m_taskbar->SetProgressState(m_hwnd, !m_controlsVisible ? TBPF_NOPROGRESS
    : m_paused ? TBPF_PAUSED : TBPF_NORMAL);
}

bool TaskbarComponentWin::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
  Q_UNUSED(eventType);
  auto* event = static_cast<MSG*>(message);
  if (!event || event->hwnd != m_hwnd) return false;
  if (m_taskbarCreated && event->message == m_taskbarCreated)
    initializeTaskbar(); // Also restores controls after Explorer restarts.
  if (event->message != WM_COMMAND || HIWORD(event->wParam) != THBN_CLICKED)
    return false;
  switch (LOWORD(event->wParam))
  {
    case PreviousButton: onPrevClicked(); break;
    case PauseButton: onPauseClicked(); break;
    case NextButton: onNextClicked(); break;
    default: return false;
  }
  if (result) *result = 0;
  return true;
}

void TaskbarComponentWin::sendMediaAction(const QString& action)
{
  // SMTC calls arrive from WinRT threads; InputComponent belongs to the GUI.
  QMetaObject::invokeMethod(this, [action] {
    InputComponent::Get().sendAction(action);
  }, Qt::QueuedConnection);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onPauseClicked()
{
  sendMediaAction("play_pause");
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onNextClicked()
{
  sendMediaAction("next");
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onPrevClicked()
{
  sendMediaAction("previous");
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::playing()
{
  setControlsVisible(true);
  setPaused(false);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::paused()
{
  setPaused(true);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::stopped()
{
  setControlsVisible(false);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setControlsVisible(bool value)
{
  m_controlsVisible = value;
  updateTaskbarButtons();

  if (m_initialized)
  {
    m_systemControls->put_PlaybackStatus(MediaPlaybackStatus::MediaPlaybackStatus_Stopped);
    m_systemControls->put_IsEnabled(value);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setProgress(quint64 value)
{
  if (!m_taskbar || !m_hwnd || !m_controlsVisible)
    return;
  const qint64 duration = PlayerComponent::Get().getDuration();
  if (duration > 0)
    m_taskbar->SetProgressValue(m_hwnd, qMin(value, quint64(duration)), quint64(duration));
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setPaused(bool value)
{
  m_paused = value;
  updateTaskbarButtons();

  if (m_initialized)
  {
    auto status = value ? MediaPlaybackStatus::MediaPlaybackStatus_Paused : MediaPlaybackStatus::MediaPlaybackStatus_Playing;
    m_systemControls->put_PlaybackStatus(status);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::initializeMediaTransport(HWND hwnd)
{
  ComPtr<ISystemMediaTransportControlsInterop> interop;
  auto hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(), &interop);
  if (FAILED(hr))
  {
    qWarning() << "Failed instantiating interop object";
    return;
  }

  hr = interop->GetForWindow(hwnd, IID_PPV_ARGS(&m_systemControls));
  if (FAILED(hr))
  {
    qWarning() << "Failed to GetForWindow";
    return;
  }

  auto handler = Callback<
    ITypedEventHandler<
      SystemMediaTransportControls*,
      SystemMediaTransportControlsButtonPressedEventArgs*>>(
    [this](ISystemMediaTransportControls* sender, ISystemMediaTransportControlsButtonPressedEventArgs* args) -> HRESULT {
      return buttonPressed(sender, args);
    });
  hr = m_systemControls->add_ButtonPressed(handler.Get(), &m_buttonPressedToken);
  if (FAILED(hr))
  {
    qWarning() << "Failed to add callback handler";
    return;
  }
  m_buttonSubscribed = true;

  hr = m_systemControls->put_IsEnabled(false);

  hr = m_systemControls->put_IsPlayEnabled(true);
  hr = m_systemControls->put_IsPauseEnabled(true);
  hr = m_systemControls->put_IsPreviousEnabled(true);
  hr = m_systemControls->put_IsNextEnabled(true);
  hr = m_systemControls->put_IsStopEnabled(true);
  hr = m_systemControls->put_IsRewindEnabled(true);
  hr = m_systemControls->put_IsFastForwardEnabled(true);
  hr = m_systemControls->put_IsChannelDownEnabled(true);
  hr = m_systemControls->put_IsChannelUpEnabled(true);

  hr = m_systemControls->get_DisplayUpdater(&m_displayUpdater);
  if (FAILED(hr))
  {
    qWarning() << "Failed to get Display updater";
    return;
  }

  m_initialized = true;
  qInfo() << "SystemMediaTransportControls successfully initialized";
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onMetaData(const QVariantMap& meta, QUrl baseUrl)
{
  if (!m_initialized)
    return;

  HRESULT hr;
  auto mediaType = meta["MediaType"].toString();

  hr = m_displayUpdater->ClearAll();
  if (FAILED(hr))
  {
    qWarning() << "Failed to clear display metadata";
    return;
  }

  if (mediaType == "Video")
  {
    setVideoMeta(meta);
  }
  else // if (mediaType == "Audio") most likely
  {
    setAudioMeta(meta);
  }

  setThumbnail(meta, baseUrl);

  hr = m_displayUpdater->Update();
  if (FAILED(hr))
  {
    qWarning() << "Failed to update the display";
    return;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setAudioMeta(const QVariantMap& meta)
{
  HRESULT hr;

  hr = m_displayUpdater->put_Type(MediaPlaybackType::MediaPlaybackType_Music);
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the media type to music";
    return;
  }

  ComPtr<IMusicDisplayProperties> musicProps;
  hr = m_displayUpdater->get_MusicProperties(musicProps.GetAddressOf());
  if (FAILED(hr))
  {
    qWarning() << "Failed to get music properties";
    return;
  }

  auto artist = meta["Artists"].toStringList().join(", ");
  hr = musicProps->put_Artist(HStringReference((const wchar_t*)artist.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the music's artist";
    return;
  }

  auto title = meta["Name"].toString();
  hr = musicProps->put_Title(HStringReference((const wchar_t*)title.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the music's title";
    return;
  }

  auto albumArtist = meta["AlbumArtist"].toString();
  hr = musicProps->put_AlbumArtist(HStringReference((const wchar_t*)albumArtist.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the music's album artist";
    return;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setVideoMeta(const QVariantMap& meta)
{
  HRESULT hr;

  hr = m_displayUpdater->put_Type(MediaPlaybackType::MediaPlaybackType_Video);
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the media type to video";
    return;
  }

  ComPtr<IVideoDisplayProperties> videoProps;
  hr = m_displayUpdater->get_VideoProperties(videoProps.GetAddressOf());
  if (FAILED(hr))
  {
    qWarning() << "Failed to get video properties";
    return;
  }

  auto title = meta["Name"].toString();
  hr = videoProps->put_Title(HStringReference((const wchar_t*)title.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the video title";
    return;
  }

  if (meta["Type"] == "Episode")
  {
    auto subtitle = meta["SeriesName"].toString();
    hr = videoProps->put_Subtitle(HStringReference((const wchar_t*)subtitle.utf16()).Get());
    if (FAILED(hr))
    {
      qWarning() << "Failed to set the video subtitle";
      return;
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setThumbnail(const QVariantMap& meta, QUrl baseUrl)
{
  QString imgUrl;
  HRESULT hr;

  auto images = meta["ImageTags"].toMap();
  if (images.contains("Primary"))
  {
    auto itemId = meta["Id"].toString();
    auto imgTag = images["Primary"].toString();
    QUrlQuery query;
    query.addQueryItem("tag", imgTag);
    baseUrl.setPath("/Items/" + itemId + "/Images/Primary");
    baseUrl.setQuery(query);
    imgUrl = baseUrl.toString();
  }
  else if (meta.contains("AlbumId") && meta.contains("AlbumPrimaryImageTag")
           && meta["AlbumId"].canConvert<QString>()
           && meta["AlbumPrimaryImageTag"].canConvert<QString>())
  {
    auto itemId = meta["AlbumId"].toString();
    auto imgTag = meta["AlbumPrimaryImageTag"].toString();
    QUrlQuery query;
    query.addQueryItem("tag", imgTag);
    baseUrl.setPath("/Items/" + itemId + "/Images/Primary");
    baseUrl.setQuery(query);
    imgUrl = baseUrl.toString();
  }
  else
  {
    qDebug() << "No Primary image found. Do nothing";
    return;
  }

  

  ComPtr<IRandomAccessStreamReferenceStatics> streamRefFactory;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference).Get(),
    &streamRefFactory);
  if (FAILED(hr))
  {
    qWarning() << "Failed instantiating stream factory";
    return;
  }

  ComPtr<IUriRuntimeClassFactory> uriFactory;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(), &uriFactory);
  if (FAILED(hr))
  {
    qWarning() << "Failed instantiating uri factory";
    return;
  }

  ComPtr<IUriRuntimeClass> uri;
  hr = uriFactory->CreateUri(HStringReference((const wchar_t*)imgUrl.utf16()).Get(), &uri);
  if (FAILED(hr))
  {
    qWarning() << "Failed to create uri";
    return;
  }

  hr = streamRefFactory->CreateFromUri(uri.Get(), &m_thumbnail);
  if (FAILED(hr))
  {
    qWarning() << "Failed to create stream from uri";
    return;
  }

  hr = m_displayUpdater->put_Thumbnail(m_thumbnail.Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set thumbnail";
    return;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
HRESULT TaskbarComponentWin::buttonPressed(ISystemMediaTransportControls* sender,
  ISystemMediaTransportControlsButtonPressedEventArgs* args)
{
  Q_UNUSED(sender);
  SystemMediaTransportControlsButton button;
  auto hr = args->get_Button(&button);
  if (FAILED(hr))
  {
    qWarning() << "Failed to get the pressed button";
    return hr;
  }

  switch (button)
  {
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Play:
      sendMediaAction("play_pause");
      qDebug() << "Received play button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Pause:
      sendMediaAction("play_pause");
      qDebug() << "Received pause button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Next:
      sendMediaAction("next");
      qDebug() << "Received next button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Previous:
      sendMediaAction("previous");
      qDebug() << "Received previous button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Stop:
      sendMediaAction("stop");
      qDebug() << "Received stop button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_FastForward:
      sendMediaAction("seek_forward");
      qDebug() << "Received seek_forward button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Rewind:
      sendMediaAction("seek_backward");
      qDebug() << "Received seek_backward button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_ChannelUp:
      sendMediaAction("channelup");
      qDebug() << "Received channelup button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_ChannelDown:
      sendMediaAction("channeldown");
      qDebug() << "Received channeldown button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Record:
      qDebug() << "Received unsupported button press";
      break;
  }

  return S_OK;
}
