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

#include <cstddef>
#include <cstdint>

namespace deluge::synth {

/// @brief The oscillator sources a sound has.
inline constexpr size_t kSourceCount = 2;

/// @brief A source's oscillator.
enum class OscType : uint8_t {
	SINE,
	TRIANGLE,
	SQUARE,
	ANALOG_SQUARE,
	SAW,
	ANALOG_SAW_2,
	SUPERSAW,
	WAVETABLE,
	SAMPLE,
	DX7,
	INPUT_L,
	INPUT_R,
	INPUT_STEREO,
};

} // namespace deluge::synth
