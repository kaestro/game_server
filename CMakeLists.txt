cmake_minimum_required(VERSION 3.10)
project(MyGameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EXECUTABLE_NAME MyGameServer)

set(SOURCES
    main.cpp
    src/server.cpp
    src/EchoClientHandler.cpp
    src/AsyncEchoClientHandler.cpp
)

include_directories(include)

add_executable(${EXECUTABLE_NAME} ${SOURCES})

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
