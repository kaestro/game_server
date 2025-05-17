#include "../include/Server.h"
#include "../include/EchoClientHandler.h"

#include <arpa/inet.h> // For htons, inet_pton
#include <cerrno>      // For errno
#include <cstring>     // For strlen, memset, strerror
#include <iostream>
#include <netinet/in.h> // For sockaddr_in
#include <sys/socket.h> // For socket, bind, listen, accept, setsockopt
#include <sys/time.h>   // For struct timeval
#include <unistd.h>     // For close(), read()

Server::Server(HandlerType handler_type, int port)
    : port_(port), main_socket_fd_(-1) {
  if (port_ <= 0 || port_ > 65535) {
    std::cerr << "Server Error: Invalid port number " << port_
              << ". Using default port " << DEFAULT_SERVER_PORT << "."
              << std::endl;
    port_ = DEFAULT_SERVER_PORT;
  }

  if (handler_type != HandlerType::ECHO) {
    std::cerr << "Server Error: Invalid handler type specified. Only ECHO is "
              << "supported at the moment." << std::endl;
    client_handler_ = nullptr;
  } else {
    client_handler_ = std::make_unique<EchoClientHandler>();
  }
}

Server::~Server() { stop(); }

bool Server::start() {
  if (!client_handler_) {
    std::cerr
        << "Server Error: Server was not initialized correctly. Cannot start."
        << std::endl;
    return false;
  }

  // IPv4, TCP, auto protocol
  if ((main_socket_fd_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Server: socket failed");
    return false;
  }

  // 소켓 옵션 설정: 소켓 종료 후 바로 재사용 가능하도록 설정
  int opt = 1;
  if (setsockopt(main_socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    perror("Server: setsockopt SO_REUSEADDR failed");
    close_socket(main_socket_fd_);
    return false;
  }

  struct sockaddr_in address;
  // sin represents socket internet address
  address.sin_family = AF_INET; // 주소 체계: IPv4
  // sin_addr: 32비트 IP 주소
  // 모든 네트워크 인터페이스에서 수신: ex. eth0, wlan0, lo
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (bind(main_socket_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Server: bind failed");
    close_socket(main_socket_fd_);
    return false;
  }

  if (listen(main_socket_fd_, 3) < 0) {
    perror("Server: listen failed");
    close_socket(main_socket_fd_);
    return false;
  }

  std::cout << "서버가 포트 " << port_ << "에서 대기 중입니다..." << std::endl;

  while (true) {
    int client_socket;
    socklen_t addrlen = sizeof(address);
    if ((client_socket = accept(main_socket_fd_, (struct sockaddr *)&address,
                                &addrlen)) < 0) {
      perror("Server: accept failed");
      continue;
    }

    std::cout << "클라이언트 (" << client_socket << ")가 연결되었습니다."
              << std::endl;

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
                   sizeof tv) < 0) {
      perror("Server: setsockopt(SO_RCVTIMEO) for client_socket failed");
      close_socket(client_socket);
      continue;
    }

    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv,
                   sizeof tv) < 0) {
      perror("Server: setsockopt(SO_SNDTIMEO) for client_socket failed");
      close_socket(client_socket);
      continue;
    }

    const char *connect_success_msg = "Server: Connection successful!\n";
    if (send(client_socket, connect_success_msg, strlen(connect_success_msg),
             0) < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        std::cerr << "Server: Send timeout for initial message to client "
                  << client_socket << std::endl;
      } else {
        std::cerr
            << "Server: failed to send connection success message to client "
            << client_socket << " with errno " << errno << ": "
            << strerror(errno) << std::endl;
      }
      close_socket(client_socket);
      continue;
    }

    client_handler_->handle_client(client_socket);
    std::cout << "Server: Client (" << client_socket
              << ") handled, waiting for next connection..." << std::endl;
  }

  return true;
}

void Server::stop() {
  if (main_socket_fd_ != -1) {
    close_socket(main_socket_fd_);
    std::cout << "Server: Listener socket closed. Server stopped." << std::endl;
  }
}

void Server::close_socket(int &socket_fd) {
  if (socket_fd != -1) {
    close(socket_fd);
    socket_fd = -1;
  }
}
