#include "../include/EchoClientHandler.h"
#include <cstring>      // For strlen
#include <iostream>     // For std::cout, perror
#include <sys/socket.h> // For send()
#include <unistd.h>     // For close(), read(), send()

const size_t BUFFER_SIZE_ECHOHANDLER = 1024;

void EchoClientHandler::handle_client(int client_socket) {
  char buffer[BUFFER_SIZE_ECHOHANDLER];
  ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE_ECHOHANDLER);

  if (bytes_read < 0) {
    perror("EchoClientHandler: read failed");
    close(client_socket);
    return;
  }
  if (bytes_read == 0) {
    std::cout << "EchoClientHandler: Client connection closed by peer."
              << std::endl;
    close(client_socket);
    return;
  }

  if (bytes_read < static_cast<ssize_t>(BUFFER_SIZE_ECHOHANDLER)) {
    buffer[bytes_read] = '\0';
  } else {
    buffer[BUFFER_SIZE_ECHOHANDLER - 1] = '\0';
    std::cout
        << "EchoClientHandler: Buffer received is larger than buffer size."
        << std::endl;
  }

  std::cout << "EchoClientHandler: Received message: " << buffer << std::endl;

  // 한 번에 send가 일부만 전송할 수 있으므로, 반복문으로 처리 통해 안정성 확보
  ssize_t bytes_sent = 0;
  while (bytes_sent < bytes_read) {
    ssize_t bytes_sent_this_time =
        send(client_socket, buffer + bytes_sent, bytes_read - bytes_sent, 0);
    if (bytes_sent_this_time < 0) {
      perror("EchoClientHandler: send failed");
      break;
    }
    bytes_sent += bytes_sent_this_time;
  }
  if (bytes_sent < bytes_read) {
    std::cout << "EchoClientHandler: Partial message sent." << std::endl;
  } else {
    std::cout << "EchoClientHandler: Echo message sent." << std::endl;
  }

  close(client_socket);
  std::cout << "EchoClientHandler: Client connection closed." << std::endl;
}
