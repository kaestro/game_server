#include <arpa/inet.h> // For sockaddr_in, inet_pton
#include <cstring>
#include <iostream>
#include <netinet/in.h> // For sockaddr_in
#include <sys/socket.h> // For socket, bind, listen, accept
#include <unistd.h>     // For close(), read()

// 클라이언트 핸들러 인터페이스 (추상 기본 클래스)
class IClientHandler {
public:
  virtual ~IClientHandler() {}
  virtual void handle_client(int client_socket) = 0;
};

// 에코 서버를 위한 클라이언트 핸들러 구현
class EchoClientHandler : public IClientHandler {
public:
  // input: fd of client socket
  // limit the size of buffer to 1024 for simplicity
  // close the client socket after handling for simplicity
  void handle_client(int client_socket) override {
    char buffer[1024] = {0};
    int bytes_read = read(client_socket, buffer, 1024);
    if (bytes_read < 0) {
      perror("read failed");
      close(client_socket);
      return;
    }
    if (bytes_read == 0) {
      std::cout << "클라이언트 연결이 종료되었습니다." << std::endl;
      close(client_socket);
      return;
    }

    std::cout << "수신된 메시지: " << buffer << std::endl;
    send(client_socket, buffer, bytes_read, 0);
    std::cout << "메시지: " << buffer << "를 에코했습니다." << std::endl;

    close(client_socket);
    std::cout << "클라이언트 연결이 종료되었습니다." << std::endl;
  }
};

class Server {
public:
  Server(int port = 8080) : port_(port), main_socket_fd(-1) {}

  bool start() {
    // IPv4, TCP, auto protocol
    if ((main_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      perror("socket failed");
      return false;
    }

    struct sockaddr_in address;
    // sin represents socket internet address
    address.sin_family = AF_INET; // 주소 체계: IPv4
    // sin_addr: 32비트 IP 주소
    // 모든 네트워크 인터페이스에서 수신: ex. eth0, wlan0, lo
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(main_socket_fd, (struct sockaddr *)&address, sizeof(address)) <
        0) {
      perror("bind failed");
      close(main_socket_fd);
      return false;
    }

    if (listen(main_socket_fd, 3) < 0) {
      perror("listen");
      close(main_socket_fd);
      return false;
    }

    std::cout << "서버가 포트 " << port_ << "에서 대기 중입니다..."
              << std::endl;

    int client_socket;
    socklen_t addrlen = sizeof(address);
    if ((client_socket = accept(main_socket_fd, (struct sockaddr *)&address,
                                &addrlen)) < 0) {
      perror("accept");
      close(main_socket_fd);
      return false;
    }

    std::cout << "클라이언트가 연결되었습니다." << std::endl;

    // 클라이언트에게 연결 성공 메시지 전송
    const char *connect_success_msg = "서버에 연결되었습니다!\n";
    send(client_socket, connect_success_msg, strlen(connect_success_msg), 0);

    // 여기서는 단일 클라이언트 처리를 위해 바로 객체를 생성하고 호출합니다.
    // 다중 클라이언트 처리를 위해서는 이 부분을 스레드 생성 로직으로 변경해야
    // 합니다. 클라이언트 소켓은 기본 에코 서버에서는 핸들러 내에서 닫힙니다.
    EchoClientHandler handler;
    handler.handle_client(client_socket);

    return true;
  }

  void stop() {
    if (main_socket_fd != -1) {
      close(main_socket_fd);
      main_socket_fd = -1;
      std::cout << "서버가 종료되었습니다." << std::endl;
    }
  }

private:
  int port_;
  int main_socket_fd;
};
