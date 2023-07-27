#pragma once
#include "memory/general_memory_allocator.h"
#include <cstddef>

namespace deluge::memory {

/**
 * @brief A simple GMA wrapper that conforms to the C++ Allocator trait spec
 * (see: https://en.cppreference.com/w/cpp/named_req/Allocator)
 *
 * @tparam T The type to allocate for
 */
template <typename T>
class fallback_allocator {
public:
	using value_type = T;

	constexpr fallback_allocator() noexcept = default;

	template <typename U>
	constexpr fallback_allocator(const fallback_allocator<U>&) noexcept {};

	[[nodiscard]] T* allocate(std::size_t n) noexcept {
		if (n == 0) {
			return nullptr;
		}
		return static_cast<T*>(
		    generalMemoryAllocator.alloc(n * sizeof(T), nullptr, false, true, false, nullptr, false));
	}

	void deallocate(T* p, std::size_t n) { generalMemoryAllocator.dealloc(p); }

	template <typename U>
	bool operator==(const deluge::memory::fallback_allocator<U>& o) {
		return true;
	}

	template <typename U>
	bool operator!=(const deluge::memory::fallback_allocator<U>& o) {
		return false;
	}
};
} // namespace deluge::memory
