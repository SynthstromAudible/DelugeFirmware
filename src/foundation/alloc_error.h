#pragma once

#include <cstdint>
#include <expected>

namespace deluge {

/// @brief Why a fallible allocation failed.
enum class AllocError : uint8_t {
	OUT_OF_MEMORY, ///< the heap could not supply the memory
};

/// @brief The result of a fallible allocating operation.
template <class T>
using AllocResult = std::expected<T, AllocError>;

} // namespace deluge
