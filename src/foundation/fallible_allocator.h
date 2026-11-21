#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <new>

namespace deluge {

/// @brief An allocator with a failure path: `try_allocate(n)` (n > 0) returns null, never throws or stops.
template <class A, class T>
concept FallibleAllocator = requires(A a, T* p, std::size_t n) {
	{ a.try_allocate(n) } noexcept -> std::same_as<T*>;
	a.deallocate(p, n);
};

/// @brief A fallible allocator over the nothrow global `operator new`, for headers that must stay BSP-free (the
/// firmware's nothrow `new` routes to the SDRAM heap, src/deluge/memory/operators.cpp).
template <class T>
struct nothrow_new_allocator {
	using value_type = T;

	constexpr nothrow_new_allocator() noexcept = default;
	template <class U>
	constexpr nothrow_new_allocator(const nothrow_new_allocator<U>& /*other*/) noexcept {}

	/// @brief Memory for @p n objects, or null.
	/// @param n Element count; must be > 0.
	/// @return The memory, or nullptr.
	[[nodiscard]] T* try_allocate(std::size_t n) noexcept {
		if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
			return nullptr;
		}
		return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{alignof(T)}, std::nothrow));
	}
	void deallocate(T* p, std::size_t /*n*/) noexcept { ::operator delete(p, std::align_val_t{alignof(T)}); }

	template <class U>
	constexpr bool operator==(const nothrow_new_allocator<U>& /*other*/) const noexcept {
		return true;
	}
};

} // namespace deluge
