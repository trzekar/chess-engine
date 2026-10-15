cmake_minimum_required(VERSION 3.20)
project(chess_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# -march=native tunes Release for the CPU of the machine that builds it. CI turns it off
# (-DNATIVE=OFF): runner CPUs vary, and clang 18 rejects -march=native on AVX10 CPUs.
option(NATIVE "Optimize Release builds for the CPU of the build machine" ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
if(NATIVE)
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=native")
endif()

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

add_executable(engine src/main.cpp src/position.cpp src/attacks.cpp src/movegen.cpp)
target_compile_options(engine PRIVATE ${WARNING_FLAGS})

# Tests. Run with: ctest --test-dir <build dir> --output-on-failure
enable_testing()

add_executable(test_types tests/test_types.cpp)
target_include_directories(test_types PRIVATE src)
# The tests check with assert(), which -DNDEBUG removes. -UNDEBUG comes after
# the build-type flags on the command line, so asserts stay on in Release too.
target_compile_options(test_types PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_types COMMAND test_types)

add_executable(test_fen tests/test_fen.cpp src/position.cpp src/attacks.cpp)
target_include_directories(test_fen PRIVATE src)
target_compile_options(test_fen PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_fen COMMAND test_fen)

add_executable(test_attacks tests/test_attacks.cpp src/position.cpp src/attacks.cpp)
target_include_directories(test_attacks PRIVATE src)
target_compile_options(test_attacks PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_attacks COMMAND test_attacks)

add_executable(test_movegen tests/test_movegen.cpp
  src/position.cpp src/attacks.cpp src/movegen.cpp)
target_include_directories(test_movegen PRIVATE src)
target_compile_options(test_movegen PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_movegen COMMAND test_movegen)