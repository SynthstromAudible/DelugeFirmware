/// @file
/// @brief The mod FX cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/mod_fx_cases.cpp) render through `effects::ModFx` over a ring from
/// the app's heap, which the host specs do not link. They check the mod_fx list's names, lanes and line count through
/// these, and the device run is where the component is timed and its output checked.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void ModFxCase::setup(std::size_t /*n*/) {
}

void ModFxCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
