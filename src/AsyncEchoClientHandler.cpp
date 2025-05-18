#include "../include/AsyncEchoClientHandler.h"
#include <arpa/inet.h>  // For htons, inet_pton
#include <netinet/in.h> // For sockaddr_in

AsyncEchoClientHandler::AsyncEchoClientHandler() : epoll_fd_(-1) {
  std::cout << "AsyncEchoClientHandler created." << std::endl;
}

AsyncEchoClientHandler::~AsyncEchoClientHandler() {
  std::cout << "AsyncEchoClientHandler destroying..." << std::endl;
  if (epoll_fd_ != -1) {
    close(epoll_fd_);
    epoll_fd_ = -1;
  }
  std::cout << "AsyncEchoClientHandler destroyed." << std::endl;
}

bool AsyncEchoClientHandler::initialize_server_socket(int &server_socket_fd,
                                                      int port) {
  if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("AsyncEchoClientHandler: socket creation failed");
    return false;
  }

  int opt = 1;
  if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    perror("AsyncEchoClientHandler: setsockopt SO_REUSEADDR failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }

  set_non_blocking(server_socket_fd);

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_socket_fd, (struct sockaddr *)&address, sizeof(address)) <
      0) {
    perror("AsyncEchoClientHandler: bind failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }

  if (listen(server_socket_fd, SOMAXCONN) < 0) {
    perror("AsyncEchoClientHandler: listen failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }
  std::cout << "AsyncEchoClientHandler: Server socket initialized "
               "(non-blocking) and listening on port "
            << port << std::endl;
  return true;
}

void AsyncEchoClientHandler::run_server_loop(int server_socket_fd) {
  if ((epoll_fd_ = epoll_create1(0)) < 0) {
    perror("AsyncEchoClientHandler: epoll_create1 failed");
    return;
  }

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = server_socket_fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_socket_fd, &ev) < 0) {
    perror("AsyncEchoClientHandler: epoll_ctl_add for server_socket_fd failed");
    close(epoll_fd_);
    epoll_fd_ = -1;
    return;
  }

  std::vector<struct epoll_event> events(ASYNC_MAX_EPOLL_EVENTS);
  std::cout
      << "AsyncEchoClientHandler: Starting asynchronous server loop (epoll)..."
      << std::endl;

  while (true) {
    int n_fds =
        epoll_wait(epoll_fd_, events.data(), ASYNC_MAX_EPOLL_EVENTS, -1);

    if (n_fds < 0) {
      if (errno == EINTR)
        continue;
      perror("AsyncEchoClientHandler: epoll_wait failed");
      break;
    }

    for (int i = 0; i < n_fds; ++i) {
      int current_fd = events[i].data.fd;
      uint32_t current_events = events[i].events;

      if ((current_events & EPOLLERR) || (current_events & EPOLLHUP)) {
        std::cerr << "AsyncEchoClientHandler: epoll error/hup on socket "
                  << current_fd << std::endl;
        remove_client_from_epoll(current_fd, false);
        continue;
      }

      if (current_fd == server_socket_fd) {
        handle_new_connection(server_socket_fd);
      } else {
        if (client_states_.count(current_fd) == 0) {
          std::cerr << "AsyncEchoClientHandler: Event on unknown client FD "
                    << current_fd << std::endl;
          epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, current_fd, nullptr);
          close(current_fd);
          continue;
        }
        if (current_events & EPOLLIN) {
          handle_client_read(current_fd);
        }
        if (client_states_.count(current_fd) > 0 &&
            (current_events & EPOLLOUT)) {
          handle_client_write(current_fd);
        }
      }
    }
  }
  std::cout << "AsyncEchoClientHandler: Server loop on socket "
            << server_socket_fd << " terminated." << std::endl;
}

void AsyncEchoClientHandler::add_client_to_epoll(int client_socket) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = client_socket;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_socket, &ev) < 0) {
    perror("AsyncEchoClientHandler: epoll_ctl_add client_socket failed");
    close(client_socket);
  } else {
    client_states_[client_socket] = AsyncClientState();
    std::cout << "AsyncEchoClientHandler: Client (" << client_socket
              << ") added to epoll." << std::endl;
    const char *greeting =
        "AsyncEchoClientHandler: Connection successful (Async)!\n";
    AsyncClientState &state = client_states_[client_socket];
    state.write_buffer_.assign(greeting, greeting + strlen(greeting));

    // send greeting for the user to know that the connection is successful
    ssize_t sent_bytes = send(client_socket, state.write_buffer_.data(),
                              state.write_buffer_.size(), 0);
    if (sent_bytes < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ev.events = EPOLLIN | EPOLLOUT;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev);
      } else {
        perror("AsyncEchoClientHandler: send greeting failed");
        remove_client_from_epoll(client_socket);
      }
    } else if (static_cast<size_t>(sent_bytes) < state.write_buffer_.size()) {
      state.write_buffer_.erase(state.write_buffer_.begin(),
                                state.write_buffer_.begin() + sent_bytes);
      ev.events = EPOLLIN | EPOLLOUT;
      epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev);
    } else {
      state.write_buffer_.clear();
    }
  }
}

void AsyncEchoClientHandler::remove_client_from_epoll(int client_socket,
                                                      bool log_disconnection) {
  close(client_socket);
  client_states_.erase(client_socket);
  if (log_disconnection) {
    std::cout << "AsyncEchoClientHandler: Client (" << client_socket
              << ") removed from epoll and disconnected." << std::endl;
  }
}

void AsyncEchoClientHandler::handle_new_connection(int server_socket_fd) {
  while (true) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_socket = accept(
        server_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    if (client_socket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("AsyncEchoClientHandler: accept failed");
        break;
      }
    }
    set_non_blocking(client_socket);
    add_client_to_epoll(client_socket);
  }
}

void AsyncEchoClientHandler::handle_client_read(int client_socket) {
  AsyncClientState &state = client_states_[client_socket];
  char buffer[ASYNC_READ_BUFFER_SIZE];
  ssize_t bytes_read;

  while (true) {
    bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      perror("AsyncEchoClientHandler: read error");
      remove_client_from_epoll(client_socket);
      return;
    } else if (bytes_read == 0) {
      remove_client_from_epoll(client_socket);
      return;
    } else {
      buffer[bytes_read] = '\0';
      std::cout << "AsyncEchoClientHandler: Received from (" << client_socket
                << "): " << buffer << std::endl;
      state.write_buffer_.insert(state.write_buffer_.end(), buffer,
                                 buffer + bytes_read);
    }
  }

  if (!state.write_buffer_.empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
      perror("AsyncEchoClientHandler: epoll_ctl_mod for EPOLLOUT failed after "
             "read");
      remove_client_from_epoll(client_socket);
    }
  }
}

void AsyncEchoClientHandler::handle_client_write(int client_socket) {
  AsyncClientState &state = client_states_[client_socket];

  if (state.write_buffer_.empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_socket;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev);
    return;
  }

  ssize_t bytes_sent;
  while (!state.write_buffer_.empty()) {
    bytes_sent = send(client_socket, state.write_buffer_.data(),
                      state.write_buffer_.size(), 0);
    if (bytes_sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      perror("AsyncEchoClientHandler: send error");
      remove_client_from_epoll(client_socket);
      return;
    } else if (bytes_sent == 0) {
      std::cerr << "AsyncEchoClientHandler: send returned 0 for socket "
                << client_socket << std::endl;
      break;
    }
    state.write_buffer_.erase(state.write_buffer_.begin(),
                              state.write_buffer_.begin() + bytes_sent);
  }

  if (state.write_buffer_.empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_socket;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev);
  }
}

// This method is part of IClientHandler interface but not actively used for I/O
// in this async model.
void AsyncEchoClientHandler::handle_client(int client_socket) {
  std::cout << "AsyncEchoClientHandler::handle_client called for socket: "
            << client_socket
            << ". This is unexpected in the epoll model as run_server_loop "
               "manages I/O directly."
            << std::endl;
  // If this were to be used, it should be non-blocking or for one-time setup.
  // For this design, all I/O is driven by epoll events in run_server_loop.
  // Perhaps close the socket if it reaches here unexpectedly through a wrong
  // configuration. remove_client_from_epoll(client_socket); // Example: treat
  // as error
}

void AsyncEchoClientHandler::shutdown() {
  std::cout << "AsyncEchoClientHandler: Shutdown called." << std::endl;
  if (epoll_fd_ != -1) {
    std::cout << "AsyncEchoClientHandler: Closing epoll_fd (" << epoll_fd_
              << ")." << std::endl;
    close(epoll_fd_);
    epoll_fd_ = -1;
  }
  std::vector<int> client_fds_to_close;
  for (auto const &[fd, val] : client_states_) {
    client_fds_to_close.push_back(fd);
  }
  for (int fd : client_fds_to_close) {
    std::cout << "AsyncEchoClientHandler: Closing client socket (" << fd
              << ") during shutdown." << std::endl;
    close(fd);
  }
  client_states_.clear();
  std::cout << "AsyncEchoClientHandler: All client states cleared."
            << std::endl;
}

void AsyncEchoClientHandler::set_non_blocking(int socket_fd) {
  // fcntl is a function that gets or sets the flags of the socket
  // O_NONBLOCK is a flag that sets the socket to non-blocking
  int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags == -1) {
    perror("AsyncEchoClientHandler: fcntl F_GETFL");
    return;
  }
  if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("AsyncEchoClientHandler: fcntl F_SETFL O_NONBLOCK");
  }
}
