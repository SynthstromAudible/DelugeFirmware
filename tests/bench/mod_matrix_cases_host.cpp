/// @file
/// @brief The mod-matrix cases' host stand-in: setup and blocks that do nothing.
/// @details The device cases (src/deluge/bench/mod_matrix_cases.cpp) build a PatchCableSet and allocate from the app's
/// heaps, which the host specs do not link. These cover the list's names, lanes and line count.

#include "bench/cases.h"

#include <cstddef>

namespace deluge::bench {

// These cover the list, not the cases' behaviour.

void MatrixCase::setup(std::size_t /*n*/) {
}
void MatrixCase::block(std::size_t /*n*/) {
}

} // namespace deluge::bench
