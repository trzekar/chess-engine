#pragma once
#include <algorithm>
#include <chrono>

using milliseconds = std::chrono::milliseconds;

// Time left on the clock after the move, for the delay of the GUI and the operating system.
constexpr milliseconds MIN_TIME_DIFFERENCE = milliseconds(50);
constexpr milliseconds MIN_TIME_FOR_MOVE = milliseconds(1);

// Budget for one move of the side to move: time_left / 20 + increment / 2, the basic formula
// from chessprogramming.org/Time_Management. About 20 more moves are expected, and half the
// increment can be spent at once because it comes back after every move.
constexpr milliseconds time_for_move(const milliseconds time_left, const milliseconds increment) {
    milliseconds time = time_left / 20 + increment / 2;

    time = std::min(time, time_left - MIN_TIME_DIFFERENCE);
    time = std::max(time, MIN_TIME_FOR_MOVE);

    return time;
}

// If n iterations used more than half of the time, the next iteration probably wouldn't finish before
// time runs out.
constexpr milliseconds soft_limit(milliseconds budget) { return budget / 2; }