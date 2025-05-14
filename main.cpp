#include "include/Server.h"
#include <iostream>

int main() {
  Server game_server;
  if (game_server.start()) {
    std::cout << "서버 로직이 (단일 클라이언트 처리 후) 완료되었습니다."
              << std::endl;
  }
  game_server.stop();

  return 0;
}
