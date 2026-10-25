#pragma once
#include <chrono>
#include <algorithm>


using milliseconds = std::chrono::milliseconds;

// Kept on the clock after the move, for GUI and OS latency.
constexpr milliseconds MIN_TIME_DIFFERENCE = milliseconds(50);
// Floor for a nearly empty clock; think completes depth 1 regardless of the budget.
constexpr milliseconds MIN_TIME_FOR_MOVE = milliseconds(1);

// time_left / 20 + increment / 2 (chessprogramming.org/Time_Management): plan for about 20
// more moves; half the increment is safe to spend, as it comes back after every move.
constexpr milliseconds time_for_move(const milliseconds time_left, const milliseconds increment){
    milliseconds time = time_left / 20 + increment / 2;

    time = std::min(time, time_left - MIN_TIME_DIFFERENCE);
    time = std::max(time, MIN_TIME_FOR_MOVE);

    return time;
}