#include "../include/Server.h"
#include "../include/EchoClientHandler.h"

#include <arpa/inet.h> // For htons, inet_pton
#include <cstring>     // For strlen, memset (if used elsewhere, perror)
#include <iostream>
#include <netinet/in.h> // For sockaddr_in
#include <sys/socket.h> // For socket, bind, listen, accept
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

  struct sockaddr_in address;
  // sin represents socket internet address
  address.sin_family = AF_INET; // 주소 체계: IPv4
  // sin_addr: 32비트 IP 주소
  // 모든 네트워크 인터페이스에서 수신: ex. eth0, wlan0, lo
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (bind(main_socket_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Server: bind failed");
    close(main_socket_fd_);
    main_socket_fd_ = -1;
    return false;
  }

  if (listen(main_socket_fd_, 3) < 0) {
    perror("Server: listen failed");
    close(main_socket_fd_);
    main_socket_fd_ = -1;
    return false;
  }

  std::cout << "서버가 포트 " << port_ << "에서 대기 중입니다..." << std::endl;

  int client_socket;
  socklen_t addrlen = sizeof(address);
  if ((client_socket = accept(main_socket_fd_, (struct sockaddr *)&address,
                              &addrlen)) < 0) {
    perror("Server: accept failed");
    close(main_socket_fd_);
    return false;
  }

  std::cout << "클라이언트가 연결되었습니다." << std::endl;

  const char *connect_success_msg = "Server: Connection successful!\n";
  if (send(client_socket, connect_success_msg, strlen(connect_success_msg), 0) <
      0) {
    perror("Server: failed to send connection success message");
  }

  // 여기서는 단일 클라이언트 처리를 위해 바로 객체를 생성하고 호출합니다.
  // 다중 클라이언트 처리를 위해서는 이 부분을 스레드 생성 로직으로 변경해야
  // 합니다. 클라이언트 소켓은 기본 에코 서버에서는 핸들러 내에서 닫힙니다.
  client_handler_->handle_client(client_socket);

  return true;
}

void Server::stop() {
  if (main_socket_fd_ != -1) {
    close(main_socket_fd_);
    main_socket_fd_ = -1;
    std::cout << "Server: Listener socket closed. Server stopped." << std::endl;
  }
}
