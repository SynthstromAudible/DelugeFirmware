#pragma once
#include "memory/general_memory_allocator.h"
#include "util/exceptions.h"
#include <cstddef>
extern "C" {
void abort(void); // this is defined in reset_handler.S
}
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

	[[nodiscard]] T* allocate(std::size_t n) noexcept(false) {
		if (n == 0) {
			return nullptr;
		}
		void* addr = GeneralMemoryAllocator::get().allocExternal(n * sizeof(T));
		if (addr != nullptr) {
			return static_cast<T*>(addr);
		}
		throw deluge::exception::BAD_ALLOC;
	}

	void deallocate(T* p, std::size_t n) { GeneralMemoryAllocator::get().deallocExternal(p); }

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
