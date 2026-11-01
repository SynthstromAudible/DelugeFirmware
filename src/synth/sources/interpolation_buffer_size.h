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

#include "definitions_cxx.hpp" // InterpolationMode, kInterpolationMaxNumSamples
#include <bit>
#include <cstdint>

namespace deluge::synth {

/// @brief How many input samples the sample reader interpolates over for @p phase_increment.
/// @details Linear interpolation (2) for the LINEAR mode, and, as the render falls behind, for ever lower pitches:
/// under the highest @p cpu_direness (14) everything from two octaves below the sample's own pitch up. Otherwise the
/// windowed sinc's full width.
/// @param phase_increment The read rate, Q8.24: 2^24 plays the sample at its own pitch.
/// @param cpu_direness    How far behind the engine's render is running, 0 when on time.
[[nodiscard]] constexpr int32_t interpolation_buffer_size(InterpolationMode mode, int32_t phase_increment,
                                                          int32_t cpu_direness) {
	if (mode == InterpolationMode::LINEAR) {
		return 2;
	}
	if (cpu_direness != 0) {
		// The read rate's octave: 25 for the sample's own pitch, 26 an octave up
		const int32_t octave = 32 - std::countl_zero(static_cast<uint32_t>(phase_increment));
		if (octave >= 26 - (cpu_direness >> 2)) {
			return 2;
		}
	}
	return kInterpolationMaxNumSamples;
}

} // namespace deluge::synth
