#include "DiscordIpc.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <QtEndian>

namespace { const quint32 MaxPayload = 64 * 1024; }

DiscordIpc::DiscordIpc(QObject* parent, const QString& pipePrefix)
  : QObject(parent), m_pipePrefix(pipePrefix)
{
  m_retry.setSingleShot(true);
  m_timeout.setSingleShot(true);
  m_update.setSingleShot(true);
  m_socket.setReadBufferSize(MaxPayload + 8);
  connect(&m_retry, &QTimer::timeout, this, &DiscordIpc::connectPipe);
  connect(&m_timeout, &QTimer::timeout, this, [this]() { reconnect(15000); });
  connect(&m_update, &QTimer::timeout, this, &DiscordIpc::sendActivity);
  connect(&m_socket, &QLocalSocket::connected, this, [this]() {
    writeFrame(0, QJsonDocument(QJsonObject{{"v", 1}, {"client_id", m_applicationId}}).toJson(QJsonDocument::Compact));
  });
  connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordIpc::readFrames);
  connect(&m_socket, &QLocalSocket::disconnected, this, [this]() { reconnect(15000); });
  connect(&m_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error), this,
          [this](QLocalSocket::LocalSocketError) { reconnect(m_pipeIndex == 0 ? 15000 : 100); });
}

void DiscordIpc::configure(bool enabled, const QString& applicationId)
{
  const QString id = applicationId.trimmed();
  enabled = enabled && QRegularExpression(QStringLiteral("^[0-9]{17,20}$")).match(id).hasMatch();
  if (m_enabled == enabled && m_applicationId == id)
    return;
  shutdown();
  m_applicationId = id;
  m_enabled = enabled;
  m_pipeIndex = 0;
  if (m_enabled)
    m_retry.start(0);
}

void DiscordIpc::shutdown()
{
  m_enabled = false;
  m_retry.stop();
  m_update.stop();
  m_timeout.stop();
  // Closing this IPC connection removes this process's presence, also on app exit.
  m_ready = false;
  m_socket.abort();
  m_input.clear();
  m_pendingNonce.clear();
  m_hasAcknowledged = false;
  m_lastWrite.invalidate();
}

void DiscordIpc::connectPipe()
{
  if (!m_enabled)
    return;
  m_timeout.start(5000);
  const QString pipe = m_pipePrefix + QString::number(m_pipeIndex);
  m_pipeIndex = (m_pipeIndex + 1) % 10;
  m_socket.connectToServer(pipe, QIODevice::ReadWrite);
}

void DiscordIpc::reconnect(int delayMs)
{
  m_ready = false;
  m_update.stop();
  m_timeout.stop();
  // abort() may emit disconnected synchronously; avoid recursive reconnects.
  m_socket.blockSignals(true);
  m_socket.abort();
  m_socket.blockSignals(false);
  m_input.clear();
  m_pendingNonce.clear();
  m_hasAcknowledged = false;
  m_lastWrite.invalidate();
  if (m_enabled)
    m_retry.start(delayMs);
}

bool DiscordIpc::writeFrame(quint32 opcode, const QByteArray& payload)
{
  QByteArray frame(8, '\0');
  qToLittleEndian<quint32>(opcode, reinterpret_cast<uchar*>(frame.data()));
  qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), reinterpret_cast<uchar*>(frame.data() + 4));
  frame.append(payload);
  if (m_socket.write(frame) != frame.size())
  {
    reconnect(15000);
    return false;
  }
  m_socket.flush();
  return true;
}

void DiscordIpc::setActivity(const QJsonObject& activity)
{
  m_activity = activity;
  sendActivity();
}

void DiscordIpc::sendActivity()
{
  if (!m_ready || !m_pendingNonce.isEmpty() || (m_hasAcknowledged && m_activity == m_acknowledged))
    return;
  // Coalesce rapid seeks and SyncPlay corrections. Stop/clear is never delayed.
  if (!m_activity.isEmpty() && m_lastWrite.isValid() && m_lastWrite.elapsed() < 5000)
  {
    m_update.start(static_cast<int>(5000 - m_lastWrite.elapsed()));
    return;
  }
  m_update.stop();
  m_sent = m_activity;
  m_pendingNonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QJsonValue activity = m_sent.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_sent);
  const QJsonObject payload{{"cmd", "SET_ACTIVITY"}, {"nonce", m_pendingNonce},
    {"args", QJsonObject{{"pid", static_cast<double>(QCoreApplication::applicationPid())}, {"activity", activity}}}};
  if (writeFrame(1, QJsonDocument(payload).toJson(QJsonDocument::Compact)))
  {
    m_lastWrite.start();
    m_timeout.start(10000);
  }
}

void DiscordIpc::readFrames()
{
  m_input.append(m_socket.readAll());
  if (m_input.size() > static_cast<int>(2 * (MaxPayload + 8)))
  {
    reconnect(15000);
    return;
  }
  while (m_input.size() >= 8)
  {
    const quint32 opcode = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(m_input.constData()));
    const quint32 length = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(m_input.constData() + 4));
    if (length > MaxPayload || opcode > 4)
    {
      reconnect(15000);
      return;
    }
    if (m_input.size() < static_cast<int>(8 + length))
      return;
    const QByteArray payload = m_input.mid(8, static_cast<int>(length));
    m_input.remove(0, static_cast<int>(8 + length));
    if (opcode == 3)
    {
      if (!writeFrame(4, payload))
        return;
      continue;
    }
    if (opcode == 4)
      continue;
    if (opcode != 1)
    {
      reconnect(15000);
      return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
      reconnect(15000);
      return;
    }
    const QJsonObject message = document.object();
    if (message.value("evt").toString() == QStringLiteral("ERROR"))
    {
      // Error text can echo the activity. Log only the numerical code.
      qWarning("Discord Rich Presence rejected an update (code %d)", message.value("data").toObject().value("code").toInt());
      reconnect(30000);
      return;
    }
    if (!m_ready && message.value("cmd").toString() == QStringLiteral("DISPATCH") &&
        message.value("evt").toString() == QStringLiteral("READY"))
    {
      m_timeout.stop();
      m_ready = true;
      sendActivity();
    }
    else if (!m_pendingNonce.isEmpty() && message.value("nonce").toString() == m_pendingNonce)
    {
      m_timeout.stop();
      m_pendingNonce.clear();
      m_acknowledged = m_sent;
      m_hasAcknowledged = true;
      sendActivity();
    }
  }
}
