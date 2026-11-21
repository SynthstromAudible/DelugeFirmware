#pragma once
#include "definitions_cxx.hpp"
#include <expected>
#include <memory>

namespace deluge {
/// @brief Room for @p n objects from @p Alloc, owned by a unique_ptr that gives it back.
/// @return The memory (null for @p n == 0), or Error::INSUFFICIENT_RAM when the allocator has none.
template <typename T, template <typename> typename Alloc>
std::expected<std::unique_ptr<T, void (*)(T*)>, Error> allocate_unique(std::size_t n) {
	T* p = n != 0 ? Alloc<T>{}.try_allocate(n) : nullptr;
	if (n != 0 && p == nullptr) {
		return std::unexpected{Error::INSUFFICIENT_RAM};
	}
	return std::unique_ptr<T, void (*)(T*)>{p, [](T* ptr) { Alloc<T>{}.deallocate(ptr, 0); }};
}
} // namespace deluge
