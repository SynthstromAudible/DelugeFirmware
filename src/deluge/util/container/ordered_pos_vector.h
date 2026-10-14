#pragma once

// The sorted container is defined by the model (deluge_project); this header exposes it under the app's name and
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
