#include "../include/Server.h"
#include "../include/AsyncEchoClientHandler.h"
#include "../include/EchoClientHandler.h"

#include <iostream>

Server::Server(HandlerType handler_type, int port)
    : handler_type_(handler_type), port_(port), main_socket_fd_(-1) {
  if (port_ <= 0 || port_ > 65535) {
    std::cerr << "Server Error: Invalid port number " << port_
              << ". Using default port " << DEFAULT_SERVER_PORT << "."
              << std::endl;
    port_ = DEFAULT_SERVER_PORT;
  }

  if (handler_type_ == HandlerType::ECHO) {
    client_handler_ = std::make_unique<EchoClientHandler>();
    std::cout << "Server configured for ECHO (synchronous) mode." << std::endl;
  } else if (handler_type_ == HandlerType::ASYNC_ECHO) {
    client_handler_ = std::make_unique<AsyncEchoClientHandler>();
    std::cout << "Server configured for ASYNC_ECHO (asynchronous) mode."
              << std::endl;
  } else {
    std::cerr << "Server Error: Unknown handler type specified. Exiting."
              << std::endl;
    client_handler_ = nullptr;
  }
}

Server::~Server() { stop(); }

void Server::close_fd(int &fd) {
  if (fd != -1) {
    close(fd);
    fd = -1;
  }
}

bool Server::start() {
  if (!client_handler_) {
    std::cerr << "Server Error: Client handler not initialized. Cannot start."
              << std::endl;
    return false;
  }

  if (!client_handler_->initialize_server_socket(main_socket_fd_, port_)) {
    std::cerr << "Server Error: Failed to initialize server socket via handler."
              << std::endl;
    if (main_socket_fd_ != -1) {
      close_fd(main_socket_fd_);
    }
    return false;
  }

  std::cout << "Server: Starting handler's server loop..." << std::endl;
  client_handler_->run_server_loop(main_socket_fd_);

  std::cout << "Server: Handler's server loop has terminated." << std::endl;
  return true;
}

void Server::stop() {
  std::cout << "Server: stop() called." << std::endl;
  if (client_handler_) {
    std::cout << "Server: Requesting handler to shutdown..." << std::endl;
    client_handler_->shutdown();
  }

  if (main_socket_fd_ != -1) {
    std::cout << "Server: Closing main listening socket (" << main_socket_fd_
              << ")." << std::endl;
    close_fd(main_socket_fd_);
  }
  std::cout << "Server: Shutdown sequence finished." << std::endl;
}
