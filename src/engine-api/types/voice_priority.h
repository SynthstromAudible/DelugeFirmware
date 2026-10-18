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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace deluge::engine {

/// @brief How readily an engine's voices are stolen when the host's voice budget runs out; higher keeps them longer.
/// Saved as the integer `priority` attribute.
enum class VoicePriority : uint8_t {
	LOW,
	MEDIUM,
	HIGH,
};

/// @brief The words a file writes for each VoicePriority, in its order: the stored numbers.
inline constexpr std::array<std::string_view, 3> kVoicePriorityWords{"0", "1", "2"};

static_assert(std::to_underlying(VoicePriority::HIGH) + 1 == kVoicePriorityWords.size());
static_assert(
    [] {
	    for (std::size_t i = 0; i < kVoicePriorityWords.size(); ++i) {
		    const std::string_view word = kVoicePriorityWords[i];
		    if (word.size() != 1 || word[0] != static_cast<char>('0' + i)) {
			    return false;
		    }
	    }
	    return true;
    }(),
    "each voice priority's word is its index's number");

} // namespace deluge::engine
