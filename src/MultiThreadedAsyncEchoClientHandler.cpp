#include "../include/MultiThreadedAsyncEchoClientHandler.h"
#include <arpa/inet.h>  // For htons, inet_pton
#include <netinet/in.h> // For sockaddr_in
#include <utility>      // for std::move

MultiThreadedAsyncEchoClientHandler::MultiThreadedAsyncEchoClientHandler(
    int num_threads)
    : epoll_fd_(-1), num_threads_(num_threads), stop_threads_(false) {
  if (num_threads_ <= 0) {
    std::cerr
        << "MultiThreadedAsyncEchoClientHandler: Invalid number of threads "
        << num_threads_ << ". Using default " << DEFAULT_THREAD_POOL_SIZE << "."
        << std::endl;
    num_threads_ = DEFAULT_THREAD_POOL_SIZE;
  }

  std::cout << "MultiThreadedAsyncEchoClientHandler created with "
            << num_threads_ << " threads." << std::endl;
  std::cout
      << "MultiThreadedAsyncEchoClientHandler: Using default number of threads "
      << num_threads_ << "." << std::endl;
  thread_pool_.reserve(num_threads_);

  for (int i = 0; i < num_threads_; ++i) {
    thread_pool_.emplace_back(
        &MultiThreadedAsyncEchoClientHandler::worker_thread_function, this);
  }
}

MultiThreadedAsyncEchoClientHandler::~MultiThreadedAsyncEchoClientHandler() {
  std::cout << "MultiThreadedAsyncEchoClientHandler destroying..." << std::endl;
  shutdown();
  std::cout << "MultiThreadedAsyncEchoClientHandler destroyed." << std::endl;
}

bool MultiThreadedAsyncEchoClientHandler::initialize_server_socket(
    int &server_socket_fd, int port) {
  if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("MultiThreadedAsyncEchoClientHandler: socket creation failed");
    return false;
  }

  int opt = 1;
  if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    perror(
        "MultiThreadedAsyncEchoClientHandler: setsockopt SO_REUSEADDR failed");
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
    perror("MultiThreadedAsyncEchoClientHandler: bind failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }

  if (listen(server_socket_fd, SOMAXCONN) < 0) {
    perror("MultiThreadedAsyncEchoClientHandler: listen failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }
  std::cout << "MultiThreadedAsyncEchoClientHandler: Server socket initialized "
               "(non-blocking) and listening on port "
            << port << std::endl;
  return true;
}

void MultiThreadedAsyncEchoClientHandler::run_server_loop(
    int server_socket_fd) {
  if ((epoll_fd_ = epoll_create1(0)) < 0) {
    perror("MultiThreadedAsyncEchoClientHandler: epoll_create1 failed");
    return;
  }

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = server_socket_fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_socket_fd, &ev) < 0) {
    perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_add for "
           "server_socket_fd failed");
    close(epoll_fd_);
    epoll_fd_ = -1;
    return;
  }

  std::vector<struct epoll_event> events(MT_ASYNC_MAX_EPOLL_EVENTS);
  std::cout << "MultiThreadedAsyncEchoClientHandler: Starting asynchronous "
               "server loop (epoll)..."
            << std::endl;

  while (!stop_threads_) {
    int n_fds =
        epoll_wait(epoll_fd_, events.data(), MT_ASYNC_MAX_EPOLL_EVENTS, 100);

    if (n_fds < 0) {
      if (errno == EINTR)
        continue;
      perror("MultiThreadedAsyncEchoClientHandler: epoll_wait failed");
      break;
    }

    for (int i = 0; i < n_fds; ++i) {
      int current_fd = events[i].data.fd;
      uint32_t current_events = events[i].events;

      if ((current_events & EPOLLERR) || (current_events & EPOLLHUP)) {
        std::cerr
            << "MultiThreadedAsyncEchoClientHandler: epoll error/hup on socket "
            << current_fd << std::endl;
        remove_client_from_epoll(current_fd, false);
        continue;
      }

      if (current_fd == server_socket_fd) {
        handle_new_connection(server_socket_fd);
      } else {
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          task_queue_.emplace([this, current_fd, current_events] {
            this->process_client_event(current_fd, current_events);
          });
        }
        // Notify one worker thread to process the task which is waiting inside
        // the queue
        condition_.notify_one();
      }
    }
  }
  std::cout << "MultiThreadedAsyncEchoClientHandler: Server loop on socket "
            << server_socket_fd << " terminated." << std::endl;
}

void MultiThreadedAsyncEchoClientHandler::worker_thread_function() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      condition_.wait(lock,
                      [this] { return stop_threads_ || !task_queue_.empty(); });
      if (stop_threads_ && task_queue_.empty()) {
        return;
      }
      task = std::move(task_queue_.front());
      task_queue_.pop();
    }
    try {
      task();
    } catch (const std::exception &e) {
      std::cerr << "MultiThreadedAsyncEchoClientHandler: Worker thread caught "
                   "exception: "
                << e.what() << std::endl;
    } catch (...) {
      std::cerr << "MultiThreadedAsyncEchoClientHandler: Worker thread caught "
                   "unknown exception."
                << std::endl;
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::process_client_event(
    int client_socket, uint32_t events) {
  // Check if client still exists, as it might be removed by another thread or
  // epoll error
  bool client_exists = (client_states_.count(client_socket) > 0);

  if (!client_exists) {
    std::cout << "MultiThreadedAsyncEchoClientHandler: process_client_event "
                 "for non-existent client "
              << client_socket << ", possibly already removed." << std::endl;
    return;
  }

  if (events & EPOLLIN) {
    handle_client_read(client_socket);
  }

  client_exists = (client_states_.count(client_socket) > 0);
  if (client_exists && (events & EPOLLOUT)) {
    handle_client_write(client_socket);
  }
}

void MultiThreadedAsyncEchoClientHandler::set_non_blocking(int socket_fd) {
  int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags == -1) {
    perror("MultiThreadedAsyncEchoClientHandler: fcntl F_GETFL");
    return;
  }
  if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("MultiThreadedAsyncEchoClientHandler: fcntl F_SETFL O_NONBLOCK");
  }
}

void MultiThreadedAsyncEchoClientHandler::add_client_to_epoll(
    int client_socket) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = client_socket;

  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_socket, &ev) < 0) {
    perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_add client_socket "
           "failed");
    close(client_socket);
  } else {
    client_states_[client_socket] =
        std::make_unique<MultiThreadedClientState>();
    std::cout << "MultiThreadedAsyncEchoClientHandler: Client ("
              << client_socket << ") added to epoll." << std::endl;

    const char *greeting =
        "MultiThreadedAsyncEchoClientHandler: Connection successful (Async)!\n";

    std::unique_ptr<MultiThreadedClientState> &client_state_ptr =
        client_states_.at(client_socket);

    if (!client_state_ptr) {
      std::cerr << "MultiThreadedAsyncEchoClientHandler: Failed to create "
                   "client state for "
                << client_socket << std::endl;
      remove_client_from_epoll(client_socket);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(client_state_ptr->state_mutex_);
      client_state_ptr->write_buffer_.assign(greeting,
                                             greeting + strlen(greeting));
    }

    ssize_t sent_bytes;
    {
      std::lock_guard<std::mutex> lock(client_state_ptr->state_mutex_);
      sent_bytes = send(client_socket, client_state_ptr->write_buffer_.data(),
                        client_state_ptr->write_buffer_.size(), 0);
    }

    if (sent_bytes < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        ev.events = EPOLLIN | EPOLLOUT;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
          perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for "
                 "EPOLLOUT failed after add_client");
          remove_client_from_epoll(client_socket);
        }
      } else {
        perror("MultiThreadedAsyncEchoClientHandler: send greeting failed");
        remove_client_from_epoll(client_socket);
      }
    } else if (client_state_ptr && static_cast<size_t>(sent_bytes) <
                                       client_state_ptr->write_buffer_.size()) {
      {
        std::lock_guard<std::mutex> lock(client_state_ptr->state_mutex_);
        client_state_ptr->write_buffer_.erase(
            client_state_ptr->write_buffer_.begin(),
            client_state_ptr->write_buffer_.begin() + sent_bytes);
      }
      ev.events = EPOLLIN | EPOLLOUT;
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
        perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for "
               "EPOLLOUT failed after partial send");
        remove_client_from_epoll(client_socket);
      }
    } else if (client_state_ptr) {
      std::lock_guard<std::mutex> lock(client_state_ptr->state_mutex_);
      client_state_ptr->write_buffer_.clear();
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::remove_client_from_epoll(
    int client_socket, bool log_disconnection) {
  if (epoll_fd_ != -1 &&
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_socket, nullptr) < 0) {
    if (epoll_fd_ != -1) {
      perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_del client_socket "
             "failed");
    }
  }

  close(client_socket);
  client_states_.erase(client_socket);

  if (log_disconnection) {
    std::cout << "MultiThreadedAsyncEchoClientHandler: Client ("
              << client_socket << ") removed from epoll and disconnected."
              << std::endl;
  }
}

void MultiThreadedAsyncEchoClientHandler::handle_new_connection(
    int server_socket_fd) {
  while (true) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_socket = accept(
        server_socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    if (client_socket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("MultiThreadedAsyncEchoClientHandler: accept failed");
        break;
      }
    }
    set_non_blocking(client_socket);
    add_client_to_epoll(client_socket);
  }
}

void MultiThreadedAsyncEchoClientHandler::handle_client_read(
    int client_socket) {
  auto it = client_states_.find(client_socket);
  if (it == client_states_.end() || !it->second) {
    std::cerr << "MultiThreadedAsyncEchoClientHandler: handle_client_read for "
                 "unknown or null state client "
              << client_socket << std::endl;
    struct epoll_event ev;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_socket, &ev);
    close(client_socket);
    return;
  }
  MultiThreadedClientState &state = *(it->second);
  char buffer[MT_ASYNC_READ_BUFFER_SIZE];
  ssize_t bytes_read;

  while (true) {
    bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      perror("MultiThreadedAsyncEchoClientHandler: read error");
      remove_client_from_epoll(client_socket);
      return;
    } else if (bytes_read == 0) { // Connection closed by peer
      remove_client_from_epoll(client_socket);
      return;
    } else {
      buffer[bytes_read] = '\0';
      std::cout << "MultiThreadedAsyncEchoClientHandler: Received from ("
                << client_socket << "): " << buffer << std::endl;
      {
        std::lock_guard<std::mutex> lock(state.state_mutex_);
        state.write_buffer_.insert(state.write_buffer_.end(), buffer,
                                   buffer + bytes_read);
      }
    }
  }

  bool needs_epollout = false;
  {
    std::lock_guard<std::mutex> lock(state.state_mutex_);
    needs_epollout = !state.write_buffer_.empty();
  }

  if (needs_epollout) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
      perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for EPOLLOUT "
             "failed after read");
      remove_client_from_epoll(client_socket);
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::handle_client_write(
    int client_socket) {
  auto it = client_states_.find(client_socket);
  if (it == client_states_.end() || !it->second) {
    std::cerr << "MultiThreadedAsyncEchoClientHandler: handle_client_write for "
                 "unknown or null state client "
              << client_socket << std::endl;
    return;
  }
  MultiThreadedClientState &state = *(it->second);
  ssize_t bytes_sent;

  std::lock_guard<std::mutex> lock(state.state_mutex_);

  if (state.write_buffer_.empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN; // Nothing to write, so only listen for reads
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
      perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for EPOLLIN "
             "failed after write (empty buffer)");
      remove_client_from_epoll(client_socket);
    }
    return;
  }

  while (!state.write_buffer_.empty()) {
    bytes_sent = send(client_socket, state.write_buffer_.data(),
                      state.write_buffer_.size(), 0);
    if (bytes_sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Buffer still has data, EPOLLOUT should remain set (or be set again by
        // MOD if needed) This case implies we should keep EPOLLOUT.
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = client_socket;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
          perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for "
                 "EPOLLIN | EPOLLOUT (EAGAIN) failed during write");
          remove_client_from_epoll(client_socket);
        }
        return;
      }
      perror("MultiThreadedAsyncEchoClientHandler: send error");
      remove_client_from_epoll(client_socket);
      return;
    } else if (bytes_sent == 0) {
      std::cerr
          << "MultiThreadedAsyncEchoClientHandler: send returned 0 for socket "
          << client_socket << ". Unusual." << std::endl;
      break;
    }
    state.write_buffer_.erase(state.write_buffer_.begin(),
                              state.write_buffer_.begin() + bytes_sent);
    std::cout << "MultiThreadedAsyncEchoClientHandler: Sent " << bytes_sent
              << " bytes to (" << client_socket << ")." << std::endl;
  }

  if (state.write_buffer_.empty()) {
    // All data sent, switch back to only EPOLLIN
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
      perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for EPOLLIN "
             "failed after successful write");
      remove_client_from_epoll(client_socket);
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::shutdown() {
  std::cout << "MultiThreadedAsyncEchoClientHandler: Shutdown called."
            << std::endl;

  // Signal worker threads to stop and wake them up
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_threads_ = true;
  }
  condition_.notify_all();

  // block until all worker threads are joined for graceful shutdown
  for (std::thread &th : thread_pool_) {
    if (th.joinable()) {
      th.join();
    }
  }
  thread_pool_.clear();
  std::cout << "MultiThreadedAsyncEchoClientHandler: All worker threads joined."
            << std::endl;

  if (epoll_fd_ != -1) {
    std::cout << "MultiThreadedAsyncEchoClientHandler: Closing epoll_fd ("
              << epoll_fd_ << ")." << std::endl;
    close(epoll_fd_);
    epoll_fd_ = -1;
  }

  std::vector<int> client_fds_to_close;
  for (auto const &[fd, client_state] : client_states_) {
    client_fds_to_close.push_back(fd);
  }

  for (int fd : client_fds_to_close) {
    std::cout << "MultiThreadedAsyncEchoClientHandler: Closing client socket ("
              << fd << ") during shutdown." << std::endl;
    remove_client_from_epoll(fd, false);
  }
  client_states_.clear();

  std::cout << "MultiThreadedAsyncEchoClientHandler: All client states cleared."
            << std::endl;
}
