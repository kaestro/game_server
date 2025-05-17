#include "../include/EchoClientHandler.h"
#include <cstring>      // For strlen
#include <iostream>     // For std::cout, perror
#include <sys/socket.h> // For send()
#include <unistd.h>     // For close(), read(), send()

const size_t BUFFER_SIZE_ECHOHANDLER = 1024;

void EchoClientHandler::handle_client(int client_socket) {
  char buffer[BUFFER_SIZE_ECHOHANDLER] = {0};
  int bytes_read = read(client_socket, buffer, BUFFER_SIZE_ECHOHANDLER);

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

  std::cout << "EchoClientHandler: Received message: " << buffer << std::endl;

  if (send(client_socket, buffer, bytes_read, 0) < 0) {
    perror("EchoClientHandler: send failed");
  } else {
    std::cout << "EchoClientHandler: Echo message sent." << std::endl;
  }

  close(client_socket);
  std::cout << "EchoClientHandler: Client connection closed." << std::endl;
}
