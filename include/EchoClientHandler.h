#ifndef ECHO_CLIENT_HANDLER_H
#define ECHO_CLIENT_HANDLER_H

#include "../include/IClientHandler.h"
#include <cerrno>       // For errno in handle_client
#include <cstring>      // For strerror in handle_client
#include <iostream>     // For perror, std::cout, std::cerr in handle_client
#include <sys/socket.h> // For socket options, struct timeval (moved here if EchoClientHandler manages its own timeouts)
#include <sys/time.h>   // For struct timeval
#include <unistd.h>     // For close() in handle_client if it closes the socket

class EchoClientHandler : public IClientHandler {
public:
  EchoClientHandler();
  ~EchoClientHandler() override;

  bool initialize_server_socket(int &server_socket_fd, int port) override;
  void run_server_loop(int server_socket_fd) override;
  void handle_client(int client_socket) override;
  void shutdown() override;
};

#endif // ECHO_CLIENT_HANDLER_H
