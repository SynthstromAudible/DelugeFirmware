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

#pragma once

#include "dsp-next/core/fixed_point.h"

namespace deluge::dsp {

/// @brief A triangle distribution on [−1, 1), centred on 0 (Irwin–Hall of two), from two uniform Q31 draws: the sum of
/// their halves, each floored, so it never leaves the range.
[[nodiscard, gnu::always_inline]] constexpr Q<31> triangle(Q<31> u1, Q<31> u2) {
	return add_wrap(halve_floor(u1), halve_floor(u2));
}

} // namespace deluge::dsp
