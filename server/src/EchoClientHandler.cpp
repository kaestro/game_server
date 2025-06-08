#include "../include/EchoClientHandler.h"
#include <arpa/inet.h>  // For htons, inet_pton
#include <cerrno>       // For errno
#include <cstring>      // For strlen, strerror
#include <iostream>     // For std::cout, perror
#include <netinet/in.h> // For sockaddr_in
#include <sys/socket.h> // For send()
#include <unistd.h>     // For close(), read(), send()

const size_t BUFFER_SIZE_ECHOHANDLER = 1024;

EchoClientHandler::EchoClientHandler() {
  std::cout << "EchoClientHandler created." << std::endl;
}

EchoClientHandler::~EchoClientHandler() {
  shutdown();
  std::cout << "EchoClientHandler destroyed." << std::endl;
}

bool EchoClientHandler::initialize_server_socket(int &server_socket_fd,
                                                 int port) {
  // AF_INET is the address family for IPv4
  // SOCK_STREAM is the socket type for TCP
  // 0 is the protocol (0 means TCP)
  if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("EchoClientHandler: socket creation failed");
    return false;
  }

  // SO_REUSEADDR is a socket option that allows the socket to be reused
  // immediately after it is closed this is useful for servers that need to
  // receive connections from multiple clients like wifi, ethernet, etc.
  int opt = 1;
  if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    perror("EchoClientHandler: setsockopt SO_REUSEADDR failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }

  // IPV4, receives any IP address such as 127.0.0.1 or 0.0.0.0
  // INADDR_ANY is a special value that means "all available IP addresses"
  // it is used to bind the socket to all available interfaces,
  // this is useful for servers that need to receive connections from multiple
  // clients like wifi, ethernet, etc. htons is a function that converts a short
  // integer (16 bits) from host byte order to network byte order network byte
  // order is the standard way of representing integers in network protocols it
  // is used to convert the port number to the network byte order
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_socket_fd, (struct sockaddr *)&address, sizeof(address)) <
      0) {
    perror("EchoClientHandler: bind failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }

  if (listen(server_socket_fd, SOMAXCONN) < 0) {
    perror("EchoClientHandler: listen failed");
    close(server_socket_fd);
    server_socket_fd = -1;
    return false;
  }
  std::cout
      << "EchoClientHandler: Server socket initialized and listening on port "
      << port << std::endl;
  return true;
}

void EchoClientHandler::run_server_loop(int server_socket_fd) {
  std::cout << "EchoClientHandler: Starting synchronous server loop..."
            << std::endl;
  struct sockaddr_in client_address;
  socklen_t client_addr_len = sizeof(client_address);

  while (true) {
    int client_socket = accept(
        server_socket_fd, (struct sockaddr *)&client_address, &client_addr_len);

    if (client_socket < 0) {
      if (errno == EINTR)
        continue;
      perror("EchoClientHandler: accept failed");
      if (errno == EBADF) {
        std::cerr << "EchoClientHandler: Listening socket seems to be closed. "
                     "Stopping loop."
                  << std::endl;
        break;
      }
      continue;
    }

    std::cout << "EchoClientHandler: Client (" << client_socket
              << ") connected." << std::endl;

    handle_client(client_socket);

    std::cout << "EchoClientHandler: Client (" << client_socket
              << ") processing finished. Waiting for new connection..."
              << std::endl;
  }
  std::cout << "EchoClientHandler: Server loop on socket " << server_socket_fd
            << " terminated." << std::endl;
}

void EchoClientHandler::handle_client(int client_socket) {
  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;

  // In case of user might not send data, we set a timeout
  // Without this, malicious user might keep the connection open and not send
  // data and the server will be stuck in the accept loop
  if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
                 sizeof tv) < 0) {
    perror("EchoClientHandler: setsockopt(SO_RCVTIMEO) failed");
    close(client_socket);
    return;
  }
  if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv,
                 sizeof tv) < 0) {
    perror("EchoClientHandler: setsockopt(SO_SNDTIMEO) failed");
    close(client_socket);
    return;
  }

  const char *connect_success_msg =
      "EchoClientHandler: Connection successful (Sync)!\n";
  if (send(client_socket, connect_success_msg, strlen(connect_success_msg), 0) <
      0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      std::cerr
          << "EchoClientHandler: Send timeout for initial message to client "
          << client_socket << std::endl;
    } else {
      std::cerr << "EchoClientHandler: failed to send greeting to client "
                << client_socket << " with errno " << errno << ": "
                << strerror(errno) << std::endl;
    }
    close(client_socket);
    return;
  }

  char buffer[BUFFER_SIZE_ECHOHANDLER];
  ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE_ECHOHANDLER);

  if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      std::cerr << "EchoClientHandler: Read timeout for socket "
                << client_socket << std::endl;
    } else {
      std::cerr << "EchoClientHandler: read failed on socket " << client_socket
                << " with errno " << errno << ": " << strerror(errno)
                << std::endl;
    }
    close(client_socket);
    return;
  }

  if (bytes_read == 0) {
    std::cout
        << "EchoClientHandler: Client connection closed by peer on socket "
        << client_socket << "." << std::endl;
    close(client_socket);
    return;
  }

  if (bytes_read < BUFFER_SIZE_ECHOHANDLER) {
    buffer[bytes_read] = '\0';
  } else {
    buffer[BUFFER_SIZE_ECHOHANDLER - 1] = '\0';
    std::cout << "EchoClientHandler: Received message is too long on socket "
              << client_socket << " (bytes_read: " << bytes_read
              << ", BUFFER_SIZE_ECHOHANDLER: " << BUFFER_SIZE_ECHOHANDLER << ")"
              << std::endl;
    close(client_socket);
    return;
  }

  std::cout << "EchoClientHandler: Received from (" << client_socket
            << "): " << buffer << std::endl;

  // For send might not send all data, we need to send it in a loop
  ssize_t bytes_sent_total = 0;
  while (bytes_sent_total < bytes_read) {
    ssize_t bytes_sent_this_time =
        send(client_socket, buffer + bytes_sent_total,
             bytes_read - bytes_sent_total, 0);
    if (bytes_sent_this_time < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        std::cerr << "EchoClientHandler: Send timeout during echo to socket "
                  << client_socket << std::endl;
      } else {
        std::cerr << "EchoClientHandler: send failed on socket "
                  << client_socket << " with errno " << errno << ": "
                  << strerror(errno) << std::endl;
      }
      break;
    }
    bytes_sent_total += bytes_sent_this_time;
  }

  if (bytes_sent_total < bytes_read) {
    std::cout << "EchoClientHandler: Partial message sent to socket "
              << client_socket << "." << std::endl;
  } else {
    std::cout << "EchoClientHandler: Echo message fully sent to socket "
              << client_socket << "." << std::endl;
  }

  close(client_socket);
  std::cout << "EchoClientHandler: Client connection (" << client_socket
            << ") closed." << std::endl;
}

void EchoClientHandler::shutdown() {
  std::cout << "EchoClientHandler: Shutdown called. No specific resources to "
               "release for this handler besides what Server class manages "
               "(listening socket)."
            << std::endl;
}
