/// @file
/// @brief The pitch shift cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/pitch_shift_cases.cpp) render the sample reader and the live pitch
/// shifter, which the host specs do not link. They check the list's names and line count through these, and the
/// device run is where the cases are timed.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the case's behaviour.

void SampleReadCase::setup(std::size_t /*n*/) {
}

void SampleReadCase::block(std::size_t /*n*/) {
}

void PitchShiftCase::setup(std::size_t /*n*/) {
}

void PitchShiftCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
