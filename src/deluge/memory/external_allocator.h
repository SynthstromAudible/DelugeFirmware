#pragma once
#include "foundation/oom.h"
#include "memory/heaps.h"
#include <cstddef>
#include <limits>
namespace deluge::memory {

/**
 * @brief A simple GMA wrapper that conforms to the C++ Allocator trait spec
 * (see: https://en.cppreference.com/w/cpp/named_req/Allocator)
 * @note "external" on the Deluge refers to the specially reserved non-stealable 8MiB region of SDRAM
 *
 * @tparam T The type to allocate for
 */
template <typename T>
class external_allocator {
public:
	using value_type = T;

	constexpr external_allocator() noexcept = default;

	template <typename U>
	constexpr external_allocator(const external_allocator<U>&) noexcept {};

	/// @brief Allocate room for @p n objects, or null when the heap cannot supply it.
	/// @param n Element count; must be > 0.
	/// @return The memory, or nullptr.
	[[nodiscard]] T* try_allocate(std::size_t n) noexcept {
		if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
			return nullptr;
		}
		return static_cast<T*>(deluge::memory::alloc_external(n * sizeof(T), alignof(T)));
	}

	/// @brief Allocate room for @p n objects; out of memory stops the firmware (deluge::oom()). Code that handles
	///        running out allocates through try_allocate.
	[[nodiscard]] T* allocate(std::size_t n) {
		if (n == 0) {
			return nullptr;
		}
		T* p = try_allocate(n);
		if (p == nullptr) [[unlikely]] {
			deluge::oom();
		}
		return p;
	}

	void deallocate(T* p, std::size_t n) noexcept { deluge::memory::dealloc(p); }

	template <typename U>
	bool operator==(const deluge::memory::external_allocator<U>& o) const noexcept {
		return true;
	}

	template <typename U>
	bool operator!=(const deluge::memory::external_allocator<U>& o) const noexcept {
		return false;
	}
};
} // namespace deluge::memory
