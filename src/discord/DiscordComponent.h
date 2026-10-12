#pragma once

#include "ComponentManager.h"
#include "DiscordIpc.h"
#include <QElapsedTimer>
#include <QUrl>

class DiscordComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(DiscordComponent);
public:
  explicit DiscordComponent(QObject* parent = nullptr);
  const char* componentName() override { return "discord"; }
  bool componentExport() override { return false; }
  bool componentInitialize() override;

private:
  void publish();
  void updateSettings();
  void clear();
  DiscordIpc m_ipc;
  QTimer m_refresh;
  QElapsedTimer m_positionClock;
  QVariantMap m_item;
  QUrl m_playbackUrl;
  qint64 m_positionMs = 0;
  qint64 m_timestampStart = 0;
  qint64 m_durationMs = 0;
  bool m_playing = false;
  bool m_buffering = false;
  bool m_active = false;
};
