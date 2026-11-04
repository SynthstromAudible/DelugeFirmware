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

#include "dsp-next/core/fixed_point.h"

#include <cstdint>

/// @brief A drum's two expression inputs for one dimension, its channel level's and its finger level's (each Q7, the
/// top byte of a 16-bit expression value), combined into the Q15 the note row records: their sum lifted to Q15 and
/// held to 16 bits, as the live path holds its sum.
[[nodiscard]] inline int16_t combined_expression(int8_t channel, int8_t finger) {
	const auto sum = deluge::dsp::Q<7>::from_raw(int32_t{channel} + int32_t{finger});
	return static_cast<int16_t>(clamp_bits<16>(as_wrap<deluge::dsp::Q<15>>(sum)).raw());
}
