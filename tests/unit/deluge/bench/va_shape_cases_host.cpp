/// @file
/// @brief The virtual-analog shape cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/va_shape_cases.cpp) render through synth::va_shape, which the host
/// specs do not link. These cover the list's names, lanes and line count.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the cases' behaviour.

void VaShapeCase::setup(std::size_t /*n*/) {
}
void VaShapeCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
