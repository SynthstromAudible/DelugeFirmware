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

#include <cstdint>
#include <limits>

namespace deluge::synth {

/// @file
/// @brief The stretch ratio that undoes a sample's pitch, so its speed holds while its pitch moves.

/// @brief The ratio, Q24, that undoes @p phase_increment against @p pitch_adjust_neutral_value: their quotient,
/// saturated at 2^32 - 1 where it would pass 32 bits (decision 3 of docs/dev/time_stretcher_design.md: about 8 octaves
/// below the root, where the legacy's truncation wrapped it toward 0).
[[nodiscard]] constexpr uint32_t ratio_undoing_pitch(int32_t pitch_adjust_neutral_value, uint32_t phase_increment) {
	const uint64_t ratio = (static_cast<uint64_t>(pitch_adjust_neutral_value) << 24) / phase_increment;
	return ratio > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
	                                                    : static_cast<uint32_t>(ratio);
}

} // namespace deluge::synth
