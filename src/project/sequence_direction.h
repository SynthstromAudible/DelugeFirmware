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

#include <optional>
#include <string_view>

namespace deluge::project {

/// @brief The order a clip or note row plays its steps in. Saved as the `sequenceDirection` attribute.
enum class SequenceDirection {
	FORWARD,     ///< first step to last
	REVERSE,     ///< last step to first
	PINGPONG,    ///< forward, then reverse, alternating
	OBEY_PARENT, ///< a note row follows its clip's direction (never saved)
};

/// @brief The `sequenceDirection` attribute text for @p direction.
/// @param direction The direction.
/// @return A view of a string literal, so `.data()` is null-terminated.
[[nodiscard]] constexpr std::string_view to_string(SequenceDirection direction) noexcept {
	switch (direction) {
	case SequenceDirection::FORWARD:
		return "forward";
	case SequenceDirection::REVERSE:
		return "reverse";
	case SequenceDirection::PINGPONG:
		return "pingpong";
	case SequenceDirection::OBEY_PARENT:
		return "obeyParent";
	}
	return "forward";
}

/// @brief The direction a `sequenceDirection` attribute names.
/// @param text The attribute text.
/// @return The direction, or empty for text that names none; songs read empty as `SequenceDirection::FORWARD`.
[[nodiscard]] constexpr std::optional<SequenceDirection> parse_sequence_direction(std::string_view text) noexcept {
	for (SequenceDirection d : {SequenceDirection::FORWARD, SequenceDirection::REVERSE, SequenceDirection::PINGPONG,
	                            SequenceDirection::OBEY_PARENT}) {
		if (text == to_string(d)) {
			return d;
		}
	}
	return std::nullopt;
}

} // namespace deluge::project
