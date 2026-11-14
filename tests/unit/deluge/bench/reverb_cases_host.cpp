/// @file
/// @brief The reverb cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/reverb_cases.cpp) render through the engine's reverb, which the host
/// specs do not link. They check the reverb list's names, lanes and line count through these, and the device run is
/// where each model is timed.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void ReverbCase::setup(std::size_t /*n*/) {
}

void ReverbCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
