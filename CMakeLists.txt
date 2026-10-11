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

set(WARNING_FLAGS
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2
  -Wfloat-equal -Wold-style-cast -Wnull-dereference -Werror)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND WARNING_FLAGS
    -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
endif()

add_executable(engine src/main.cpp src/position.cpp)
target_compile_options(engine PRIVATE ${WARNING_FLAGS})

# Tests. Run with: ctest --test-dir <build dir> --output-on-failure
enable_testing()

add_executable(test_types tests/test_types.cpp)
target_include_directories(test_types PRIVATE src)
# The tests check with assert(), which -DNDEBUG removes. -UNDEBUG comes after
# the build-type flags on the command line, so asserts stay on in Release too.
target_compile_options(test_types PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_types COMMAND test_types)

add_executable(test_fen tests/test_fen.cpp src/position.cpp)
target_include_directories(test_fen PRIVATE src)
target_compile_options(test_fen PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_fen COMMAND test_fen)