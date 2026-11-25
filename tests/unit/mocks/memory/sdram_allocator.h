// Unit-test stand-in for memory/sdram_allocator.h: the host heap, with a switch to make allocations fail the way the
// real allocator does when the Deluge is out of memory.
#pragma once
#include "util/exceptions.h"
#include <cstddef>
#include <cstdlib>

namespace deluge::memory {

/// Allocations left before they start failing; negative for no limit
inline int sdramAllocationsBeforeFailure = -1;

template <typename T>
class sdram_allocator {
public:
	using value_type = T;

	constexpr sdram_allocator() noexcept = default;

	template <typename U>
	constexpr sdram_allocator(const sdram_allocator<U>&) noexcept {};

	[[nodiscard]] T* allocate(std::size_t n) noexcept(false) {
		if (sdramAllocationsBeforeFailure == 0) {
			throw deluge::exception::BAD_ALLOC;
		}
		if (sdramAllocationsBeforeFailure > 0) {
			sdramAllocationsBeforeFailure--;
		}
		return static_cast<T*>(std::malloc(n * sizeof(T)));
	}

	void deallocate(T* p, std::size_t n) { std::free(p); }

	template <typename U>
	bool operator==(const sdram_allocator<U>& o) const {
		return true;
	}
};
} // namespace deluge::memory
