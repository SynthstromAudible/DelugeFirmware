/// @file
/// @brief The DX7 cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/dx7_cases.cpp) render through the voice adapter and the DX7 engine's
/// pool, which the host specs do not link. They check the dx7 list's names, lanes and line count through these, and
/// the device run is where each engine is timed.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void Dx7Case::setup(std::size_t /*n*/) {
}

void Dx7Case::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
