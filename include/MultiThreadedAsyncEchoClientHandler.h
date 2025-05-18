#ifndef MULTI_THREADED_ASYNC_ECHO_CLIENT_HANDLER_H
#define MULTI_THREADED_ASYNC_ECHO_CLIENT_HANDLER_H

#include "../include/IClientHandler.h"
#include <cerrno>             // For errno
#include <condition_variable> // For std::condition_variable
#include <cstring>            // For strerror, memset
#include <fcntl.h>            // For fcntl
#include <functional>         // For std::function
#include <iostream>           // For logging
#include <map>                // For client_states_
#include <mutex>              // For std::mutex
#include <queue>              // For task queue
#include <sys/epoll.h>        // For epoll_fd_
#include <thread>             // For std::thread
#include <unistd.h>           // For close
#include <vector>             // For ClientState, events vector, thread pool

struct MultiThreadedClientState {
  std::vector<char> write_buffer_;
  std::mutex state_mutex_;

  MultiThreadedClientState() = default;
  MultiThreadedClientState(const MultiThreadedClientState &) = delete;
  MultiThreadedClientState &
  operator=(const MultiThreadedClientState &) = delete;
  MultiThreadedClientState(MultiThreadedClientState &&) = delete;
  MultiThreadedClientState &operator=(MultiThreadedClientState &&) = delete;
};

const int MT_ASYNC_MAX_EPOLL_EVENTS = 100;
const int MT_ASYNC_READ_BUFFER_SIZE = 1024;
const int DEFAULT_THREAD_POOL_SIZE = 4;

class MultiThreadedAsyncEchoClientHandler : public IClientHandler {
public:
  MultiThreadedAsyncEchoClientHandler(int num_threads);
  ~MultiThreadedAsyncEchoClientHandler() override;

  MultiThreadedAsyncEchoClientHandler(
      const MultiThreadedAsyncEchoClientHandler &) = delete;
  MultiThreadedAsyncEchoClientHandler &
  operator=(const MultiThreadedAsyncEchoClientHandler &) = delete;
  MultiThreadedAsyncEchoClientHandler(MultiThreadedAsyncEchoClientHandler &&) =
      delete;
  MultiThreadedAsyncEchoClientHandler &
  operator=(MultiThreadedAsyncEchoClientHandler &&) = delete;

  bool initialize_server_socket(int &server_socket_fd, int port) override;
  void run_server_loop(int server_socket_fd) override;
  void shutdown() override;

private:
  void set_non_blocking(int socket_fd);
  void add_client_to_epoll(int client_socket);
  void remove_client_from_epoll(int client_socket,
                                bool log_disconnection = true);
  void handle_new_connection(int server_socket_fd);

  // Task to be executed by worker threads
  void process_client_event(int client_socket, uint32_t events);
  void handle_client_read(int client_socket);
  void handle_client_write(int client_socket);

  // Thread pool
  void worker_thread_function();
  std::vector<std::thread> thread_pool_;
  std::queue<std::function<void()>> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_threads_ = false;

  int epoll_fd_ = -1;
  std::map<int, std::unique_ptr<MultiThreadedClientState>> client_states_;
  int num_threads_ = DEFAULT_THREAD_POOL_SIZE;
};

#endif // MULTI_THREADED_ASYNC_ECHO_CLIENT_HANDLER_H
