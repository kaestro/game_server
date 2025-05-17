#ifndef GAMESERVER_SERVER_H
#define GAMESERVER_SERVER_H

#include "IClientHandler.h"
#include <iostream> // For std::cout, perror (used in implementations, but good for consistency)
#include <memory>
#include <netinet/in.h> // For sockaddr_in (though used in .cpp, part of socket interface)
#include <sys/socket.h> // For socket types used in method signatures
#include <unistd.h> // For close() in potential inline destructors or simple methods

enum class HandlerType { ECHO, UNKNOWN };

const int DEFAULT_SERVER_PORT = 8080;

class Server {
public:
  Server(HandlerType handler_type, int port = DEFAULT_SERVER_PORT);
  ~Server();
  bool start();
  void stop();

private:
  void close_socket(int &socket_fd);

  int port_;
  int main_socket_fd_ = -1;
  std::unique_ptr<IClientHandler> client_handler_;
};

#endif // GAMESERVER_SERVER_H
