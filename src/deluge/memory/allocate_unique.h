#pragma once
#include "definitions_cxx.hpp"
#include "util/exceptions.h"
#include <expected>
#include <memory>

namespace deluge {
template <typename T, template <typename> typename Alloc>
std::expected<std::unique_ptr<T, void (*)(T*)>, Error> allocate_unique(std::size_t n) {
	try {
		return std::unique_ptr<T, void (*)(T*)>{Alloc<T>().allocate(n), [](T* ptr) { Alloc<T>().deallocate(ptr, 0); }};
	} catch (deluge::exception e) {
		return std::unexpected{Error::INSUFFICIENT_RAM};
	}
}
} // namespace deluge
