/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "util/lookuptables/lookuptables.h" // noteIntervalTable

#include <cstdint>
#include <limits>

/// @file
/// @brief An audio clip's phase increments: Q24 of the engine's sample rate (2^24 plays a sample at the engine's rate),
/// held under 2^31, the most an increment holds.

/// @brief @p increment scaled by the Q24 @p ratio: the 64-bit product's Q24, held under 2^31, the product itself held
/// where a ratio past 2^32 would carry it past 64 bits.
[[nodiscard]] constexpr int32_t scaled_increment(uint32_t increment, uint64_t ratio) {
	if ((ratio >> 32) != 0 && increment != 0 && ratio > std::numeric_limits<uint64_t>::max() / increment) {
		return std::numeric_limits<int32_t>::max();
	}
	const uint64_t scaled = (uint64_t{increment} * ratio) >> 24;
	return scaled > std::numeric_limits<int32_t>::max() ? std::numeric_limits<int32_t>::max()
	                                                    : static_cast<int32_t>(scaled);
}

/// @brief The increment that plays a sample of @p sample_rate transposed by @p transpose semitones (±96): the
/// semitone's Q30 interval placed by the octave, then scaled from the sample's rate to the engine's, held under 2^31
/// (from about +84, the top octaves play at the most an increment holds).
[[nodiscard]] inline int32_t transposed_neutral(int32_t transpose, uint32_t sample_rate, uint32_t engine_rate) {
	const int32_t note_within_octave = static_cast<uint16_t>(transpose + 240) % 12;
	const int32_t octave = (static_cast<uint16_t>(transpose + 120) / 12) - 10;
	const int32_t interval = noteIntervalTable[note_within_octave];
	// The interval is Q30 of 1×: six octaves up is the interval itself (Q24), lower octaves shift it down, higher up
	uint64_t neutral =
	    octave <= 6 ? static_cast<uint64_t>(interval >> (6 - octave)) : static_cast<uint64_t>(interval) << (octave - 6);
	if (sample_rate != engine_rate) {
		neutral = neutral * sample_rate / engine_rate;
	}
	return neutral > std::numeric_limits<int32_t>::max() ? std::numeric_limits<int32_t>::max()
	                                                     : static_cast<int32_t>(neutral);
}
