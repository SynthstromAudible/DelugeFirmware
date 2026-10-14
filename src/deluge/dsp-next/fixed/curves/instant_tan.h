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

#include "util/fixedpoint.h"

#include <array>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief tan from 0 to 1.44 radians in 64 steps plus one, Q28: entry i is tan(0.0225·i)·2^28.
// clang-format off
alignas(16) inline constexpr std::array<int32_t, 65> kTanTable = {
0, 6040817, 12087756, 18146962, 24224633, 30327039, 36460554, 42631679,
48847074, 55113581, 61438264, 67828436, 74291696, 80835969, 87469544, 94201124,
101039871, 107995463, 115078152, 122298831, 129669107, 137201382, 144908946, 152806078,
160908164, 169231824, 177795063, 186617437, 195720245, 205126751, 214862435, 224955281,
235436122, 246339020, 257701735, 269566249, 281979405, 294993645, 308667898, 323068637,
338271146, 354361056, 371436208, 389608933, 409008861, 429786415, 452117178, 476207408,
502301079, 530688941, 561720325, 595818723, 633502600, 675413648, 722355745, 775349708,
835711820, 905169129, 986033315, 1081471022, 1195939485, 1335918723, 1511206022, 1737350377,
2040599645, };
// clang-format on

/// @brief tan of an angle by table (the ladders' frequency warp): bits 25 to 30 of @p input pick the entry of
/// @ref kTanTable and the 25 bits below interpolate (at Q31) toward the next.
/// @param input The angle, non-negative: 2^31 spans the table's 1.44 radians (2^25 per entry).
/// @return The tangent, Q28 like the table (each Q31 product halves it and the closing shift doubles it back).
[[nodiscard]] inline int32_t instant_tan(int32_t input) {
	const int32_t which = input >> 25;
	const int32_t toward_next = (input << 6) & 2147483647;
	const int32_t lower = kTanTable[which];
	const int32_t upper = kTanTable[which + 1];
	return (multiply_32x32_rshift32(upper, toward_next) + multiply_32x32_rshift32(lower, 2147483647 - toward_next))
	       << 1;
}

} // namespace deluge::dsp::fixed
