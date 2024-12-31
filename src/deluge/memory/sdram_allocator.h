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
 * @note The sdram_allocator allows allocation to _either_ the specially reserved "external" 8MiB region of SDRAM _OR_
 *       the remaining 56MiB "stealable" region of SDRAM
 *
 * @tparam T The type to allocate for
 */
template <typename T>
class sdram_allocator {
public:
	using value_type = T;

	constexpr sdram_allocator() noexcept = default;

	template <typename U>
	constexpr sdram_allocator(const sdram_allocator<U>&) noexcept {};

	[[nodiscard]] T* allocate(std::size_t n) noexcept(false) {
		if (n == 0) {
			return nullptr;
		}
		void* addr = GeneralMemoryAllocator::get().allocLowSpeed(n * sizeof(T));
		if (addr == nullptr) [[unlikely]] {
			throw deluge::exception::BAD_ALLOC;
		}
		return static_cast<T*>(addr);
	}

	void deallocate(T* p, std::size_t n) { GeneralMemoryAllocator::get().dealloc(p); }

	template <typename U>
	bool operator==(const sdram_allocator<U>& o) {
		return true;
	}

	template <typename U>
	bool operator!=(const sdram_allocator<U>& o) {
		return false;
	}
};
} // namespace deluge::memory
