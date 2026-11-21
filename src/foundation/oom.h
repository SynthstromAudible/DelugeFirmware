#pragma once

#include "foundation/alloc_error.h"

#include <utility>

namespace deluge {

/// @brief Report memory exhaustion on an allocation that has no failure path, and stop.
///
/// Supplied at link time by each binary: the firmware and the sim freeze with "OOM" (src/deluge/memory/oom.cpp); the
/// spec trees throw std::bad_alloc (tests/support/mocks/oom_for_tests.cpp) so a spec can observe it.
[[noreturn]] void oom();

/// @brief The value of @p r, or oom() if it holds an error.
/// @param r A fallible operation's result.
/// @return The value.
template <class T>
[[nodiscard]] T or_oom(AllocResult<T>&& r) {
	if (!r) [[unlikely]] {
		oom();
	}
	return std::move(*r);
}

/// @brief oom() if @p r holds an error.
/// @param r A fallible operation's result.
inline void or_oom(AllocResult<void> r) {
	if (!r) [[unlikely]] {
		oom();
	}
}

} // namespace deluge
