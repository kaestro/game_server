#ifndef ICLIENT_HANDLER_H
#define ICLIENT_HANDLER_H

class IClientHandler {
public:
  virtual ~IClientHandler() = default;
  virtual void handle_client(int client_socket) = 0;
};

#endif // ICLIENT_HANDLER_H
