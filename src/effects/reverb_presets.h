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
/// @brief The reverb's presets, which a gold-knob push cycles: their room sizes and dampings, in the song menu's
///        units (0 to 50).

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::effects {

/// @brief A reverb preset: its room size and damping, in the song menu's units (0 to 50).
struct ReverbPreset {
	int32_t room_size{0}; ///< The room size, 0 to 50.
	int32_t damping{0};   ///< The damping, 0 to 50.
};

/// @brief The small, medium and large rooms the reverb knob cycles through.
inline constexpr std::array<ReverbPreset, 3> kReverbPresets{
    {{.room_size = 16, .damping = 29}, {.room_size = 30, .damping = 36}, {.room_size = 44, .damping = 45}}};

namespace detail {

/// @brief The distance between two menu values, kept constexpr and dependency-free.
[[nodiscard]] constexpr int32_t menu_distance(int32_t a, int32_t b) {
	return a > b ? a - b : b - a;
}

} // namespace detail

/// @brief The preset nearest a room size and damping: the least summed distance, the first on a tie.
/// @param room_size The room size, in the song menu's units (0 to 50).
/// @param damping   The damping, in the song menu's units (0 to 50).
/// @return The preset's index into kReverbPresets.
[[nodiscard]] constexpr size_t nearest_reverb_preset(int32_t room_size, int32_t damping) {
	const auto distance_to = [&](size_t p) {
		return detail::menu_distance(room_size, kReverbPresets[p].room_size)
		       + detail::menu_distance(damping, kReverbPresets[p].damping);
	};
	size_t nearest = 0;
	int32_t lowest = distance_to(0);
	for (size_t p = 1; p < kReverbPresets.size(); ++p) {
		const int32_t distance = distance_to(p);
		if (distance < lowest) {
			lowest = distance;
			nearest = p;
		}
	}
	return nearest;
}

/// @brief The preset nearest a song's reverb, whose room size and damping are fractions of the menu's range.
/// @param room_size The room size, 0 to 1 (truncated to the menu's 0 to 50 units).
/// @param damping   The damping, 0 to 1 (truncated to the menu's 0 to 50 units).
/// @return The preset's index into kReverbPresets.
[[nodiscard]] constexpr size_t nearest_reverb_preset_of(float room_size, float damping) {
	return nearest_reverb_preset(static_cast<int32_t>(room_size * 50), static_cast<int32_t>(damping * 50));
}

static_assert(nearest_reverb_preset(30, 36) == 1);

} // namespace deluge::effects
