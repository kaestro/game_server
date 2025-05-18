#include "../include/MultiThreadedAsyncEchoClientHandler.h"
#include <arpa/inet.h>  // For htons, inet_pton
#include <netinet/in.h> // For sockaddr_in
#include <shared_mutex> // Required for std::shared_lock and std::unique_lock with std::shared_mutex
#include <utility> // for std::move

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
        std::shared_ptr<MultiThreadedClientState> client_state_for_task;
        {
          std::shared_lock<std::shared_mutex> lock(client_states_mutex_);
          auto it = client_states_.find(current_fd);
          if (it != client_states_.end()) {
            client_state_for_task = it->second;
          }
        }

        if (client_state_for_task) {
          {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            task_queue_.emplace([this, state = std::move(client_state_for_task),
                                 fd = current_fd, evts = current_events] {
              this->process_client_event(state, fd, evts);
            });
          }
          condition_.notify_one();
        } else {
          std::cerr << "MultiThreadedAsyncEchoClientHandler: Client "
                    << current_fd
                    << " not found in map when creating task. Possibly already "
                       "removed."
                    << std::endl;
        }
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
    std::shared_ptr<MultiThreadedClientState> client_state,
    int client_socket_fd, uint32_t events) {
  if (!client_state) {
    std::cout << "MultiThreadedAsyncEchoClientHandler: process_client_event "
                 "for null client_state, client "
              << client_socket_fd << ", possibly already removed." << std::endl;
    return;
  }

  if (events & EPOLLIN) {
    handle_client_read(client_state, client_socket_fd);
  }

  // Re-check client_state as handle_client_read might have invalidated it
  // by calling remove_client_from_epoll indirectly.
  // However, the shared_ptr itself is still valid if the object was removed
  // from map. The more important check is whether the socket is still
  // operational. This re-check logic is a bit tricky with shared_ptr keeping
  // the state object alive while underlying socket might be closed. For now,
  // let's assume if EPOLLOUT is set, we try to write. The write handler will
  // deal with a closed socket.
  if (events & EPOLLOUT) {
    // We need to ensure that the client_state is still associated with an
    // active client in the map before attempting a write, or that the
    // write_buffer in the state implies a pending write from a previous
    // operation. A simple check of client_state pointer is not enough if the
    // client was removed from map. However, the task was queued with a valid
    // shared_ptr. Let's rely on handle_client_write to manage errors on a
    // potentially closed socket.
    bool still_in_map = false;
    {
      std::shared_lock<std::shared_mutex> lock(client_states_mutex_);
      still_in_map = client_states_.count(client_socket_fd) > 0;
    }
    if (still_in_map) { // Only proceed if still in map, to avoid writing to a
                        // fully disconnected client state
      handle_client_write(client_state, client_socket_fd);
    } else {
      std::cout << "MultiThreadedAsyncEchoClientHandler: process_client_event "
                   "(write) "
                   "for client "
                << client_socket_fd
                << ", but it was removed from map. Skipping write."
                << std::endl;
    }
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
    std::shared_ptr<MultiThreadedClientState> new_client_state;
    {
      std::unique_lock<std::shared_mutex> lock(client_states_mutex_);
      new_client_state = std::make_shared<MultiThreadedClientState>();
      client_states_[client_socket] = new_client_state;
    }
    std::cout << "MultiThreadedAsyncEchoClientHandler: Client ("
              << client_socket << ") added to epoll and map." << std::endl;

    const char *greeting =
        "MultiThreadedAsyncEchoClientHandler: Connection successful (Async)!\n";

    if (!new_client_state) {
      std::cerr << "MultiThreadedAsyncEchoClientHandler: Failed to create "
                   "client state for "
                << client_socket << " (make_shared returned null)."
                << std::endl;
      remove_client_from_epoll(client_socket, false);
      return;
    }

    MultiThreadedClientState &client_state_ref = *new_client_state;

    {
      std::lock_guard<std::mutex> lock(client_state_ref.state_mutex_);
      client_state_ref.write_buffer_.assign(greeting,
                                            greeting + strlen(greeting));
    }

    ssize_t sent_bytes;
    {
      std::lock_guard<std::mutex> lock(client_state_ref.state_mutex_);
      if (client_state_ref.write_buffer_.empty()) {
        std::cerr << "MultiThreadedAsyncEchoClientHandler: Greeting buffer "
                     "empty for client "
                  << client_socket << ". Skipping initial send." << std::endl;
        return;
      }
      sent_bytes = send(client_socket, client_state_ref.write_buffer_.data(),
                        client_state_ref.write_buffer_.size(), 0);
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
    } else if (static_cast<size_t>(sent_bytes) <
               client_state_ref.write_buffer_.size()) {
      {
        std::lock_guard<std::mutex> lock(client_state_ref.state_mutex_);
        client_state_ref.write_buffer_.erase(
            client_state_ref.write_buffer_.begin(),
            client_state_ref.write_buffer_.begin() + sent_bytes);
      }
      ev.events = EPOLLIN | EPOLLOUT;
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket, &ev) < 0) {
        perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_mod for "
               "EPOLLOUT failed after partial send");
        remove_client_from_epoll(client_socket);
      }
    } else {
      std::lock_guard<std::mutex> lock(client_state_ref.state_mutex_);
      client_state_ref.write_buffer_.clear();
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::remove_client_from_epoll(
    int client_socket, bool log_disconnection) {
  bool actually_removed_from_map = false;
  {
    std::unique_lock<std::shared_mutex> lock(client_states_mutex_);
    if (client_states_.count(client_socket)) {
      client_states_.erase(client_socket);
      actually_removed_from_map = true;
    }
  }

  if (actually_removed_from_map) {
    if (epoll_fd_ != -1) {
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_socket, nullptr) < 0) {
        if (errno != ENOENT) {
          perror("MultiThreadedAsyncEchoClientHandler: epoll_ctl_del "
                 "client_socket failed");
        }
      }
    }
    close(client_socket);

    if (log_disconnection) {
      std::cout << "MultiThreadedAsyncEchoClientHandler: Client ("
                << client_socket
                << ") removed from map, epoll, and disconnected." << std::endl;
    }
  } else {
    if (log_disconnection) {
      std::cout << "MultiThreadedAsyncEchoClientHandler: Client ("
                << client_socket
                << ") disconnect requested, but not found in active client map "
                   "(likely already removed)."
                << std::endl;
    }
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
    std::shared_ptr<MultiThreadedClientState> state_ptr, int client_socket_fd) {
  if (!state_ptr) {
    std::cerr << "MultiThreadedAsyncEchoClientHandler: handle_client_read for "
                 "null state_ptr, client "
              << client_socket_fd << std::endl;
    remove_client_from_epoll(client_socket_fd, false);
    return;
  }

  MultiThreadedClientState &state = *state_ptr;
  char buffer[MT_ASYNC_READ_BUFFER_SIZE];
  ssize_t bytes_read;

  while (true) {
    bytes_read = read(client_socket_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      remove_client_from_epoll(client_socket_fd);
      return;
    } else if (bytes_read == 0) {
      remove_client_from_epoll(client_socket_fd);
      return;
    } else {
      buffer[bytes_read] = '\0';
      std::cout << "MultiThreadedAsyncEchoClientHandler: Received from ("
                << client_socket_fd << "): " << buffer << std::endl;
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
    ev.data.fd = client_socket_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket_fd, &ev) < 0) {
      remove_client_from_epoll(client_socket_fd);
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::handle_client_write(
    std::shared_ptr<MultiThreadedClientState> state_ptr, int client_socket_fd) {
  if (!state_ptr) {
    std::cerr << "MultiThreadedAsyncEchoClientHandler: handle_client_write for "
                 "null state_ptr, client "
              << client_socket_fd << std::endl;
    // No state to write from, client likely already being removed.
    // remove_client_from_epoll might have been called or will be by other
    // parts. We might not need to call it again here unless we are sure it's
    // required. For now, just return, as there's no buffer to access.
    return;
  }
  MultiThreadedClientState &state = *state_ptr;
  ssize_t bytes_sent;

  std::lock_guard<std::mutex> lock(state.state_mutex_);

  if (state.write_buffer_.empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_socket_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket_fd, &ev) < 0) {
      remove_client_from_epoll(client_socket_fd);
    }
    return;
  }

  while (!state.write_buffer_.empty()) {
    bytes_sent = send(client_socket_fd, state.write_buffer_.data(),
                      state.write_buffer_.size(), 0);
    if (bytes_sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = client_socket_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket_fd, &ev) < 0) {
          remove_client_from_epoll(client_socket_fd);
        }
        return;
      }
      remove_client_from_epoll(client_socket_fd);
      return;
    } else if (bytes_sent == 0) {
      std::cerr
          << "MultiThreadedAsyncEchoClientHandler: send returned 0 for socket "
          << client_socket_fd << ". Unusual. Assuming disconnection."
          << std::endl;
      remove_client_from_epoll(client_socket_fd);
      return;
    }
    state.write_buffer_.erase(state.write_buffer_.begin(),
                              state.write_buffer_.begin() + bytes_sent);
    std::cout << "MultiThreadedAsyncEchoClientHandler: Sent " << bytes_sent
              << " bytes to (" << client_socket_fd << ")." << std::endl;
  }

  if (state.write_buffer_.empty()) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_socket_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_socket_fd, &ev) < 0) {
      remove_client_from_epoll(client_socket_fd);
    }
  }
}

void MultiThreadedAsyncEchoClientHandler::shutdown() {
  std::cout << "MultiThreadedAsyncEchoClientHandler: Shutdown called."
            << std::endl;

  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_threads_ = true;
  }
  condition_.notify_all();

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
  {
    // No lock needed for client_states_ here if we are just getting keys
    // and remove_client_from_epoll handles its own locking for erase.
    // However, to prevent iterator invalidation if another thread somehow
    // modifies (though unlikely during this stage of shutdown), a shared_lock
    // is safer.
    std::shared_lock<std::shared_mutex> lock(client_states_mutex_);
    for (auto const &[fd, client_state_sh_ptr] : client_states_) {
      client_fds_to_close.push_back(fd);
    }
  }

  for (int fd : client_fds_to_close) {
    // Set log_disconnection to false as this is part of a bulk shutdown.
    remove_client_from_epoll(fd, false);
  }

  {
    std::unique_lock<std::shared_mutex> lock(client_states_mutex_);
    if (!client_states_.empty()) {
      std::cout << "MultiThreadedAsyncEchoClientHandler: Clearing "
                << client_states_.size()
                << " remaining client states during final shutdown stage."
                << std::endl;
      client_states_.clear();
    }
  }

  std::cout << "MultiThreadedAsyncEchoClientHandler: All client states cleared "
               "and sockets closed."
            << std::endl;
}
