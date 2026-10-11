//
// Created by Tobias Hieta on 24/08/15.
//

#ifndef KONVERGO_INPUTSOCKET_H
#define KONVERGO_INPUTSOCKET_H

#include "LocalJsonServer.h"
#include "InputComponent.h"
#include "shared/ApplicationScope.h"
#include "shared/Paths.h"

class InputSocket : public InputBase
{
  Q_OBJECT
public:
  explicit InputSocket(QObject* parent = nullptr) : InputBase(parent)
  {
    const QString socketName = Paths::isPortableMode()
      ? ApplicationScope::socketName(QStringLiteral("inputSocket"), true, Paths::dataDir())
      : QStringLiteral("inputSocket");
    m_server = new LocalJsonServer(socketName, this);
    connect(m_server, &LocalJsonServer::clientConnected, this, &InputSocket::clientConnected);
    connect(m_server, &LocalJsonServer::messageReceived, this, &InputSocket::messageReceived);
  }

  bool initInput() override;
  const char* inputName() override { return "socket"; };

private Q_SLOTS:
  void clientConnected(QLocalSocket* socket);
  void messageReceived(const QVariant& message);

private:
  LocalJsonServer* m_server;
};

#endif //KONVERGO_INPUTSOCKET_H
