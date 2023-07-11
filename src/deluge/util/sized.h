#pragma once
#include <cstddef>

namespace deluge {

/**
 * @brief A very simple class to make passing around objects with
 *        inherent size (i.e. variable-length arrays of pointers) simpler.
 *
 * @tparam T The type to wrap. This is used as-is, and not turned into a pointer
 *           internally for explicit communication of _what exactly_ is being Sized
 *
 * @details This should be used with C++ structured binding as
 * 		@code{.cpp}
 * 			const auto [thing, size] = get_sized_object();
 * 		@endcode
 */
template <typename T>
struct Sized {
	T value;
	size_t size;
};
} // namespace deluge
