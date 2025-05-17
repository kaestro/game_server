#include "include/Server.h"
#include <iostream>

int main() {
  Server game_server(HandlerType::ECHO);
  if (!game_server.start()) {
    std::cerr << "Server: Server start failed." << std::endl;
  }

  std::cout << "서버 프로그램의 main 함수가 종료됩니다." << std::endl;

  return 0;
}
