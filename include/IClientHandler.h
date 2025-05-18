#ifndef ICLIENT_HANDLER_H
#define ICLIENT_HANDLER_H

class IClientHandler {
public:
  virtual ~IClientHandler() = default;

  // Initializes the listening/server socket.
  // Returns true on success, false on failure.
  // server_socket_fd will be populated with the listening socket descriptor.
  // This method should be called by Server::start() before run_server_loop().
  virtual bool initialize_server_socket(int &server_socket_fd, int port) = 0;

  // Runs the main server loop (e.g., accept loop for sync, epoll loop for
  // async). Takes the already initialized server_socket_fd as a parameter.
  virtual void run_server_loop(int server_socket_fd) = 0;

  // Handles a single accepted client connection (primarily for synchronous
  // handlers). Asynchronous handlers might not use this in the traditional
  // sense if run_server_loop manages all I/O.
  virtual void handle_client(int client_socket) = 0;

  // Called when the server is stopping, to allow the handler to clean up its
  // resources.
  virtual void shutdown() = 0;
};

#endif // ICLIENT_HANDLER_H
