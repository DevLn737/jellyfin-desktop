#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

// Uses Qt's asynchronous Windows named-pipe transport; no Discord SDK DLL needed.
class DiscordIpc : public QObject
{
  Q_OBJECT
public:
  explicit DiscordIpc(QObject* parent = nullptr, const QString& pipePrefix = QStringLiteral("discord-ipc-"));
  ~DiscordIpc() override;
  void configure(bool enabled, const QString& applicationId);
  void setActivity(const QJsonObject& activity);
  void shutdown();

private:
  void connectPipe();
  void reconnect(int delayMs);
  void readFrames();
  void sendActivity();
  bool writeFrame(quint32 opcode, const QByteArray& payload);

  QLocalSocket m_socket;
  QTimer m_retry;
  QTimer m_timeout;
  QTimer m_update;
  QElapsedTimer m_lastWrite;
  QByteArray m_input;
  QString m_applicationId;
  QString m_pipePrefix;
  QString m_pendingNonce;
  QJsonObject m_activity;
  QJsonObject m_sent;
  QJsonObject m_acknowledged;
  int m_pipeIndex = 0;
  bool m_enabled = false;
  bool m_ready = false;
  bool m_hasAcknowledged = false;
};
