#pragma once
#include "memory/heaps.h"
#include "util/exceptions.h"
#include <cstddef>
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

	[[nodiscard]] T* allocate(std::size_t n) noexcept(false) {
		if (n == 0) {
			return nullptr;
		}
		void* addr = deluge::memory::alloc_external(n * sizeof(T), alignof(T));
		if (addr == nullptr) [[unlikely]] {
			throw deluge::exception::BAD_ALLOC;
		}
		return static_cast<T*>(addr);
	}

	void deallocate(T* p, std::size_t n) { deluge::memory::dealloc(p); }

	template <typename U>
	bool operator==(const deluge::memory::external_allocator<U>& o) {
		return true;
	}

	template <typename U>
	bool operator!=(const deluge::memory::external_allocator<U>& o) {
		return false;
	}
};
} // namespace deluge::memory
