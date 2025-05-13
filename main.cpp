#include "src/server.cpp"
#include <iostream>

int main() {
  Server game_server(8080);
  if (game_server.start()) {
    std::cout << "서버가 시작되었습니다." << std::endl;
  }
  game_server.stop();

  return 0;
}
