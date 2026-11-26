/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief A typed owner as one word, for a lock-free atomic: its pointer, with its alternative's index above it.

#include "model/lane_owner.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace deluge::model {

/// Where a packed owner keeps its alternative's index: above any pointer the device (32-bit) or the host (47-bit user
/// addresses) hands out.
inline constexpr unsigned kOwnerIndexShift = 56;

/// @brief @p owner as one word.
/// @param owner The owner.
/// @return Its pointer, with its alternative's index above it; 0 for none.
[[nodiscard]] inline uint64_t pack_owner(const LaneOwner& owner) {
	return std::visit(
	    [&]<typename T>(T held) -> uint64_t {
		    if constexpr (std::is_same_v<T, std::monostate>) {
			    return 0;
		    }
		    else {
			    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(held))
			           | (static_cast<uint64_t>(owner.index()) << kOwnerIndexShift);
		    }
	    },
	    owner);
}

namespace detail {
template <size_t... Index>
[[nodiscard]] LaneOwner unpack_owner(uint64_t bits, std::index_sequence<Index...> /*alternatives*/) {
	const size_t index = bits >> kOwnerIndexShift;
	const auto pointer = static_cast<uintptr_t>(bits & ((uint64_t{1} << kOwnerIndexShift) - 1));
	LaneOwner owner;
	(
	    [&] {
		    using T = std::variant_alternative_t<Index, LaneOwner>;
		    if constexpr (!std::is_same_v<T, std::monostate>) {
			    if (index == Index) {
				    owner = reinterpret_cast<T>(pointer);
			    }
		    }
	    }(),
	    ...);
	return owner;
}
} // namespace detail

/// @brief The owner @p bits packs: pack_owner's inverse.
/// @param bits A word pack_owner made.
/// @return The owner.
[[nodiscard]] inline LaneOwner unpack_owner(uint64_t bits) {
	return detail::unpack_owner(bits, std::make_index_sequence<std::variant_size_v<LaneOwner>>{});
}

} // namespace deluge::model
