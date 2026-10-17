#pragma once
#include <iosfwd>

// Runs the UCI protocol: reads commands from `in` one line at a time and writes the replies to
// `out`, until `quit` or the end of the input. Searches run on a second thread, so `isready` and
// `stop` are answered while the engine thinks. Unknown commands are ignored, as UCI asks.
void uci_loop(std::istream &in, std::ostream &out);