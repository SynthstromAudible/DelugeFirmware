#pragma once

// The sorted container is the model's (deluge_project); this keeps the app's existing includes and name working, and
// translates its failures into the app's Error.
#include "definitions_cxx.hpp"
#include "deluge-project/ordered_pos_vector.h"

#include <expected>

namespace deluge {

template <typename T, auto keyMember = &T::pos>
using OrderedPosVector = deluge::project::OrderedPosVector<T, keyMember>;
using deluge::project::AllocError;

/// @brief The app's Error for a model-container result: NONE on success, INSUFFICIENT_RAM when memory ran out.
template <class T>
[[nodiscard]] constexpr Error error_of(const std::expected<T, AllocError>& result) noexcept {
	return result ? Error::NONE : Error::INSUFFICIENT_RAM;
}

} // namespace deluge
