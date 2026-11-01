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

#include <algorithm>
#include <cstdint>
#include <utility>

namespace deluge::plugin {

/// @brief The rhythmic flavour of a synced rate. Each value is the index of that flavour's first option in the sync
/// menus, which list nine levels per flavour.
enum class SyncType : uint8_t {
	EVEN = 0,
	TRIPLET = 10,
	DOTTED = 19,
};

/// @brief A synced rate as a note length. The names hold at the default resolution; the song's tick magnitude
/// shifts the actual length. Saved as integers 0–9, converted relative to the magnitude by the song.
enum class SyncLevel : uint8_t {
	NONE,                      ///< not synced
	WHOLE,                     ///< whole note
	HALF,                      ///< half note
	QUARTER,                   ///< quarter note
	EIGHTH,                    ///< eighth note
	SIXTEENTH,                 ///< sixteenth note
	THIRTY_SECOND,             ///< 1/32
	SIXTY_FOURTH,              ///< 1/64
	ONE_HUNDRED_TWENTY_EIGHTH, ///< 1/128
	TWO_HUNDRED_FIFTY_SIXTH,   ///< 1/256
};

/// @brief The highest SyncLevel index.
inline constexpr int32_t kMaxSyncLevel = std::to_underlying(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH);

/// @brief @p level as its index, 0 (NONE) to kMaxSyncLevel, for the arithmetic sync rates are computed with.
/// @param level The sync level.
/// @return The level's index.
[[nodiscard]] constexpr int32_t index_of(SyncLevel level) noexcept {
	return std::to_underlying(level);
}

/// @brief The SyncLevel at @p index, clamped to [NONE, TWO_HUNDRED_FIFTY_SIXTH].
/// @param index A level index; out-of-range values are clamped.
/// @return The level at the clamped index.
[[nodiscard]] constexpr SyncLevel sync_level_from_index(int32_t index) noexcept {
	return static_cast<SyncLevel>(std::clamp<int32_t>(index, 0, kMaxSyncLevel));
}

/// @brief A sync level as a file stores it, from the internal value (1–9, 0 = off), relative to
/// @p input_tick_magnitude; never below 1 unless off.
/// @param internal              The internal sync level, 1–9, or 0 for off.
/// @param input_tick_magnitude  The song's input tick magnitude.
/// @return The value a file stores: 0 for off, otherwise at least 1.
[[nodiscard]] constexpr int32_t sync_level_to_file(int32_t internal, int32_t input_tick_magnitude) {
	if (internal == 0) {
		return 0;
	}
	const int32_t file = internal - 1 + input_tick_magnitude;
	return file < 1 ? 1 : file;
}

/// @brief The internal sync level (1–9, 0 = off) from the value a file stores relative to @p input_tick_magnitude.
/// @param file                  The value a file stores.
/// @param input_tick_magnitude  The song's input tick magnitude.
/// @return The internal level, clamped to 1–9, or 0 for off.
[[nodiscard]] constexpr int32_t sync_level_from_file(int32_t file, int32_t input_tick_magnitude) {
	if (file == 0) {
		return 0;
	}
	const int32_t internal = file + 1 - input_tick_magnitude;
	return internal < 1 ? 1 : (internal > 9 ? 9 : internal);
}

} // namespace deluge::plugin
