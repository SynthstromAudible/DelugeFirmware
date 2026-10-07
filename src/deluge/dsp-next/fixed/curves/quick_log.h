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

#include "util/fixedpoint.h" // clz

#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A fast log2 approximation of an unsigned value: its bit length at 2^25 per octave, plus the value lined up
/// with its leading one at bit 25 — that one adding another octave, the bits below it a straight-line fraction of the
/// octave — with bit 26 of the lined-up value cleared.
/// @param input Non-zero.
/// @return log2(input) + 2 at 2^25 per octave, linear within each octave. A value with bit 31 set is lined up by an
/// arithmetic shift, so its sign bits come in and the sum wraps (0xFFFFFFFF gives 1006632959).
[[nodiscard]] inline int32_t quick_log(uint32_t input) {
	const auto magnitude = static_cast<uint32_t>(32 - clz(input));
	// Lined up so the leading one sits at bit 25: signed, so values past 26 bits shift down arithmetically.
	const auto value = static_cast<int32_t>(input);
	const int32_t shift = 26 - static_cast<int32_t>(magnitude);
	const auto lined_up = static_cast<uint32_t>(shift >= 0 ? value << shift : value >> -shift);
	return static_cast<int32_t>((magnitude << 25) + (lined_up & ~(static_cast<uint32_t>(1) << 26)));
}

} // namespace deluge::dsp::fixed
