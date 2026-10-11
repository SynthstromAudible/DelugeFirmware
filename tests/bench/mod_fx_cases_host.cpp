/// @file
/// @brief The mod FX cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/mod_fx_cases.cpp) call the app's `ModFXProcessor` on an
/// `UnpatchedParamSet`, which pull in the param set's storage and engine code; the host specs link neither. They
/// check the mod_fx list's names, lanes and line count through these, and the device run is where the processor is
/// timed and its output checked.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

void ModFxCase::setup(std::size_t /*n*/) {
}

void ModFxCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
