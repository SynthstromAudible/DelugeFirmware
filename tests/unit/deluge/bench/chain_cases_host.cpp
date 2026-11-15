/// @file
/// @brief The effect chain cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/chain_cases.cpp) render each home's chain, whose rings and tapes come
/// from the app's heap and delay arena, which the host specs do not link. They check the effect chain list's names,
/// lanes and line count through these, and the device run is where the chains are timed.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void FxChainCase::setup(std::size_t /*n*/) {
}

void FxChainCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
