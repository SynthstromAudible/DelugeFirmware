/// @file
/// @brief The stutter cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/stutter_cases.cpp) render through the stutter, whose tape comes from the
/// app's heap, which the host specs do not link. They check the stutter list's names, lanes and line count through
/// these, and the device run is where the stutter is timed.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void StutterCase::setup(std::size_t /*n*/) {
}

void StutterCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
