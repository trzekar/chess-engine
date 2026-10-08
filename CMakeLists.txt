cmake_minimum_required(VERSION 3.20)
project(chess_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

if(MINGW)
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
else()
  set(CMAKE_CXX_FLAGS_DEBUG
      "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer")
endif()

add_executable(engine src/main.cpp)

target_compile_options(engine PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2
  -Wfloat-equal -Wold-style-cast -Wnull-dereference -Werror)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(engine PRIVATE
    -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
endif()