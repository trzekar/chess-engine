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

# The UCI loop runs the search on a second thread.
find_package(Threads REQUIRED)

add_executable(engine src/main.cpp src/position.cpp src/attacks.cpp src/movegen.cpp src/perft.cpp
  src/eval.cpp src/search.cpp src/uci.cpp)
target_compile_options(engine PRIVATE ${WARNING_FLAGS})
target_link_libraries(engine PRIVATE Threads::Threads)

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

add_executable(test_make_move tests/test_make_move.cpp
  src/position.cpp src/attacks.cpp src/movegen.cpp)
target_include_directories(test_make_move PRIVATE src)
target_compile_options(test_make_move PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_make_move COMMAND test_make_move)

add_executable(test_eval tests/test_eval.cpp
  src/eval.cpp src/position.cpp src/attacks.cpp src/movegen.cpp)
target_include_directories(test_eval PRIVATE src)
target_compile_options(test_eval PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_eval COMMAND test_eval)

add_executable(test_search tests/test_search.cpp
  src/search.cpp src/eval.cpp src/position.cpp src/attacks.cpp src/movegen.cpp)
target_include_directories(test_search PRIVATE src)
target_compile_options(test_search PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_search
  COMMAND test_search ${CMAKE_SOURCE_DIR}/tests/mate_positions.epd)

add_executable(test_uci tests/test_uci.cpp
  src/uci.cpp src/search.cpp src/eval.cpp src/position.cpp src/attacks.cpp src/movegen.cpp)
target_include_directories(test_uci PRIVATE src)
target_compile_options(test_uci PRIVATE ${WARNING_FLAGS} -UNDEBUG)
target_link_libraries(test_uci PRIVATE Threads::Threads)
add_test(NAME test_uci COMMAND test_uci)

# The fast perft suite runs in ctest. The deep one (--deep) is run directly: in CI by the
# Release jobs, locally before committing a change to move generation or make_move.
add_executable(test_perft tests/test_perft.cpp
  src/position.cpp src/attacks.cpp src/movegen.cpp src/perft.cpp)
target_include_directories(test_perft PRIVATE src)
target_compile_options(test_perft PRIVATE ${WARNING_FLAGS} -UNDEBUG)
add_test(NAME test_perft
  COMMAND test_perft ${CMAKE_SOURCE_DIR}/tests/perft_positions.epd)