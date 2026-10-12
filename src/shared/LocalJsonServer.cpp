//
// Created by Tobias Hieta on 30/08/15.
//

#include <QDebug>
#include <QFile>
#include "LocalJsonServer.h"
#include "Paths.h"

/////////////////////////////////////////////////////////////////////////////////////////
LocalJsonServer::LocalJsonServer(const QString& serverName, QObject* parent) : QObject(parent)
{
  m_server = new QLocalServer(this);
  m_serverName = Paths::socketName(serverName);

  connect(m_server, &QLocalServer::newConnection, this, &LocalJsonServer::serverClientConnected);
}

/////////////////////////////////////////////////////////////////////////////////////////
bool LocalJsonServer::listen()
{
  bool listening = m_server->listen(m_serverName);
#ifdef Q_OS_UNIX
  // Preserve legacy stale Unix socket cleanup, with one bounded retry.
  if (!listening && m_server->serverError() == QAbstractSocket::AddressInUseError
      && QFile(m_serverName).remove())
    listening = m_server->listen(m_serverName);
#endif
  // Windows named pipes are not files. A failed bind must not spin or attempt
  // to remove another application's endpoint.
  if (!listening)
  {
    qWarning() << "Failed to listen to local socket:" << m_server->errorString();
    return false;
  }

  qInfo() << "Listening to socket:" << m_serverName;
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void LocalJsonServer::serverClientConnected()
{
  QLocalSocket* socket = m_server->nextPendingConnection();
  if (socket)
  {
    m_clientSockets << socket;
    connect(socket, &QLocalSocket::readyRead, this, &LocalJsonServer::clientReadyRead);
    emit clientConnected(socket);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
bool LocalJsonServer::sendMessage(const QVariantMap& message, QLocalSocket* socket)
{
  QJsonObject obj = QJsonObject::fromVariantMap(message);

  if (obj.isEmpty())
    return false;

  QJsonDocument doc(obj);
  QByteArray data = doc.toJson(QJsonDocument::Compact);
  data.append("\r\n");

  if (!data.isEmpty())
    return (socket->write(data) == data.size());

  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
void LocalJsonServer::clientReadyRead()
{
  QLocalSocket* socket = dynamic_cast<QLocalSocket*>(sender());
  if (!socket)
    return;

  QVariantList messages = readFromSocket(socket);
  for(const QVariant& msg : messages)
    emit messageReceived(msg.toMap());
}

/////////////////////////////////////////////////////////////////////////////////////////
QVariantList LocalJsonServer::readFromSocket(QLocalSocket* socket)
{
  QVariantList lst;

  while (socket->canReadLine())
  {
    QByteArray data = socket->readLine();
    if (!data.isNull())
    {
      QJsonParseError err;
      QJsonDocument doc = QJsonDocument::fromJson(data, &err);
      if (doc.isNull())
      {
        qWarning() << "Failed to parse message from client:" << err.errorString();
        continue;
      }

      lst << doc.toVariant();
    }
  }

  return lst;
}
