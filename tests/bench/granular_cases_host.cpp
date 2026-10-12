/// @file
/// @brief The granular cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/granular_cases.cpp) render through `effects::Granular`, whose State and
/// ring come from the app's heap, which the host specs do not link. They check the granular list's names, lanes and
/// line count through these, and the device run is where the component is timed.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void GranularCase::setup(std::size_t /*n*/) {
}

void GranularCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
