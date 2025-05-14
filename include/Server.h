#ifndef GAMESERVER_SERVER_H
#define GAMESERVER_SERVER_H

#include <iostream> // For std::cout, perror (used in implementations, but good for consistency)
#include <netinet/in.h> // For sockaddr_in (though used in .cpp, part of socket interface)
#include <sys/socket.h> // For socket types used in method signatures
#include <unistd.h> // For close() in potential inline destructors or simple methods

class IClientHandler {
public:
  virtual ~IClientHandler() {}
  virtual void handle_client(int client_socket) = 0;
};

class EchoClientHandler : public IClientHandler {
public:
  void handle_client(int client_socket) override;
};

class Server {
public:
  Server(int port = 8080);
  ~Server();
  bool start();
  void stop();

private:
  int port_;
  int main_socket_fd = -1;
};

#endif // GAMESERVER_SERVER_H
