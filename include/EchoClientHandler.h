#ifndef ECHO_CLIENT_HANDLER_H
#define ECHO_CLIENT_HANDLER_H

#include "IClientHandler.h"

class EchoClientHandler : public IClientHandler {
public:
  void handle_client(int client_socket) override;
};

#endif // ECHO_CLIENT_HANDLER_H
