#ifndef ASYNC_ECHO_CLIENT_HANDLER_H
#define ASYNC_ECHO_CLIENT_HANDLER_H

#include "../include/IClientHandler.h"
#include <cerrno>      // For errno
#include <cstring>     // For strerror, memset
#include <fcntl.h>     // For fcntl
#include <iostream>    // For logging
#include <map>         // For client_states_
#include <sys/epoll.h> // For epoll_fd_
#include <unistd.h>    // For close
#include <vector>      // For ClientState, events vector

struct AsyncClientState {
  std::vector<char> write_buffer_;
};

const int ASYNC_MAX_EPOLL_EVENTS = 100;
const int ASYNC_READ_BUFFER_SIZE = 1024;

class AsyncEchoClientHandler : public IClientHandler {
public:
  AsyncEchoClientHandler();
  ~AsyncEchoClientHandler() override;

  bool initialize_server_socket(int &server_socket_fd, int port) override;
  void run_server_loop(int server_socket_fd) override;

  // This handle_client is part of the interface but not actively used
  // by this handler's epoll loop in the traditional blocking sense.
  // The epoll loop in run_server_loop manages client I/O directly.
  void handle_client(int client_socket) override;

  void shutdown() override;

private:
  void set_non_blocking(int socket_fd);
  void add_client_to_epoll(int client_socket);
  void remove_client_from_epoll(int client_socket,
                                bool log_disconnection = true);
  void handle_new_connection(int server_socket_fd);
  void handle_client_read(int client_socket);
  void handle_client_write(int client_socket);

  int epoll_fd_ = -1;
  std::map<int, AsyncClientState> client_states_;
};

#endif // ASYNC_ECHO_CLIENT_HANDLER_H
