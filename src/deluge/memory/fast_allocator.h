#pragma once
#include "memory/heaps.h"
#include "util/exceptions.h"
#include <cstddef>
namespace deluge::memory {

/**
 * @brief A C++ Allocator-trait wrapper over the fast (SRAM-preferred) heap.
 * (see: https://en.cppreference.com/w/cpp/named_req/Allocator). Routes through
 * deluge::memory::alloc_fast — the Rust deluge_alloc core (or, gate-off, the
 * legacy GeneralMemoryAllocator) — without reaching GeneralMemoryAllocator::get().
 *
 * @tparam T The type to allocate for
 */
template <typename T>
class fast_allocator {
public:
	using value_type = T;

	constexpr fast_allocator() noexcept = default;

	template <typename U>
	constexpr fast_allocator(const fast_allocator<U>&) noexcept {};

	[[nodiscard]] T* allocate(std::size_t n) noexcept(false) {
		if (n == 0) {
			return nullptr;
		}
		void* addr = deluge::memory::alloc_fast(n * sizeof(T), alignof(T));
		if (addr == nullptr) [[unlikely]] {
			throw deluge::exception::BAD_ALLOC;
		}
		return static_cast<T*>(addr);
	}

	void deallocate(T* p, std::size_t n) { deluge::memory::dealloc(p); }

	template <typename U>
	bool operator==(const fast_allocator<U>& o) {
		return true;
	}

	template <typename U>
	bool operator!=(const fast_allocator<U>& o) {
		return false;
	}
};
} // namespace deluge::memory
