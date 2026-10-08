#include "DiscordComponent.h"
#include "DiscordActivity.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include <QCoreApplication>
#include <QDateTime>

DiscordComponent::DiscordComponent(QObject* parent) : ComponentBase(parent)
{
}

bool DiscordComponent::componentInitialize()
{
  auto& player = PlayerComponent::Get();
  connect(&player, &PlayerComponent::presenceMediaChanged, this,
    [this](const QVariantMap& item, const QUrl& url, qint64 positionMs) {
      m_item = item;
      // Retain no authentication data, even in the component's private state.
      m_playbackUrl = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
      m_positionMs = positionMs;
      m_timestampStart = 0;
      m_durationMs = 0;
      m_positionClock.invalidate();
      m_active = true;
      m_playing = false;
      m_buffering = true;
      publish();
    });
  connect(&player, &PlayerComponent::stateChanged, this,
    [this](PlayerComponent::State state, PlayerComponent::State) {
      if (state == PlayerComponent::State::finished || state == PlayerComponent::State::canceled ||
          state == PlayerComponent::State::error)
      {
        clear();
        return;
      }
      if (m_playing && m_positionClock.isValid())
        m_positionMs += m_positionClock.elapsed();
      m_playing = state == PlayerComponent::State::playing;
      m_buffering = state == PlayerComponent::State::buffering;
      m_positionClock.invalidate();
      publish();
    });
  connect(&player, &PlayerComponent::positionUpdate, this, [this](quint64 positionMs) {
    const qint64 expected = m_positionMs + (m_playing && m_positionClock.isValid() ? m_positionClock.elapsed() : 0);
    const bool seek = qAbs(static_cast<qint64>(positionMs) - expected) > 1500;
    m_positionMs = static_cast<qint64>(positionMs);
    m_positionClock.start();
    if (seek)
    {
      m_timestampStart = 0;
      publish();
    }
  });
  connect(&player, &PlayerComponent::updateDuration, this, [this](qint64 durationMs) {
    if (m_active && durationMs > 0 && durationMs != m_durationMs)
    {
      m_durationMs = durationMs;
      publish();
    }
  });
  // Explicit stop also covers stopping before mpv ever enters a playback state.
  connect(&player, &PlayerComponent::presenceStopped, this, &DiscordComponent::clear);
  connect(&SettingsComponent::Get(), &SettingsComponent::sectionValueUpdate, this,
    [this](const QString& section, const QVariantMap& values) {
      if (section == QStringLiteral("main") &&
          (values.contains("discordRichPresence") || values.contains("discordApplicationId")))
        updateSettings();
    });
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &m_ipc, &DiscordIpc::shutdown);
  m_refresh.setInterval(15000);
  connect(&m_refresh, &QTimer::timeout, this, &DiscordComponent::publish);
  m_refresh.start();
  updateSettings();
  return true;
}

void DiscordComponent::updateSettings()
{
  auto& settings = SettingsComponent::Get();
  m_ipc.configure(settings.value("main", "discordRichPresence").toBool(),
                  settings.value("main", "discordApplicationId").toString());
  publish();
}

void DiscordComponent::clear()
{
  m_active = false;
  m_timestampStart = 0;
  m_durationMs = 0;
  m_playing = false;
  m_buffering = false;
  m_item.clear();
  m_playbackUrl.clear();
  m_positionClock.invalidate();
  publish();
}

void DiscordComponent::publish()
{
  if (!m_active)
  {
    m_ipc.setActivity({});
    return;
  }
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const qint64 position = m_positionMs +
    (m_playing && m_positionClock.isValid() ? m_positionClock.elapsed() : 0);
  m_timestampStart = DiscordActivity::startTimestamp(m_timestampStart, position, m_playing, now);
  m_ipc.setActivity(DiscordActivity::build(m_item, m_playbackUrl, position,
    m_playing, m_buffering, now, m_timestampStart, m_durationMs));
}
