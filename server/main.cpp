#include "../include/Server.h"
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
  HandlerType mode = HandlerType::ECHO;
  std::string mode_str = "ECHO (synchronous)";

  if (argc > 1) {
    std::string mode_arg = argv[1];
    if (mode_arg == "async") {
      mode = HandlerType::ASYNC_ECHO;
      mode_str = "ASYNC_ECHO (asynchronous)";
    } else if (mode_arg == "sync") {
      mode = HandlerType::ECHO;
      mode_str = "ECHO (synchronous)";
    } else if (mode_arg == "mtasync") {
      mode = HandlerType::MULTI_THREADED_ASYNC_ECHO;
      mode_str = "MULTI_THREADED_ASYNC_ECHO (multithreaded asynchronous)";
    } else {
      std::cout << "알 수 없는 인자입니다: " << mode_arg << ". 기본 모드("
                << mode_str << ")로 시작합니다." << std::endl;
    }
  }
  std::cout << "선택된 서버 모드: " << mode_str << std::endl;

  Server game_server(mode, DEFAULT_SERVER_PORT);

  std::cout << "서버 시작 시도 중 (" << mode_str << ")..." << std::endl;

  if (game_server.start()) {
    std::cout
        << "서버 루프가 정상적으로 종료되었습니다 (또는 핸들러에 의해 중단됨)."
        << std::endl;
  } else {
    std::cerr << "서버 시작에 실패했습니다 (핸들러 초기화 실패)." << std::endl;
  }

  std::cout << "서버 프로그램의 main 함수가 종료됩니다." << std::endl;

  return 0;
}
