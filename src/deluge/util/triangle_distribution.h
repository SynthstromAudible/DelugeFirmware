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

/// @brief A triangle-distributed Q31 value on [−1, 1), centred on 0 (Irwin–Hall), from two uniform Q31 draws: their
/// mean, each halved, so the sum never leaves the range.
[[nodiscard]] inline int32_t triangle_of(int32_t u1, int32_t u2) {
	return add_wrap(halve_floor(deluge::dsp::Q<31>::from_raw(u1)), halve_floor(deluge::dsp::Q<31>::from_raw(u2))).raw();
}
