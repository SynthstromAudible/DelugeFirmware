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

#include "dsp-next/core/types.h"

#include <cstdint>

/// @file
/// @brief Wrapping addition and subtraction of raw 32-bit words: two's complement, as the hardware's `add` and `sub`
/// compute.
///
/// A signed overflow is undefined in C++, and once a kernel's step is inlined into a loop the compiler may assume it
/// never happens. These compute through uint32_t, whose arithmetic is defined modulo 2^32, so an out-of-range result
/// wraps exactly as the target's instruction does, and they compile to that one instruction.
namespace deluge::dsp {

/// @brief @p a + @p b, wrapping modulo 2^32 on overflow.
/// @param a,b The addends, raw words of one format.
/// @return The sum, in their format.
[[nodiscard]] [[gnu::always_inline]] constexpr int32_t wrapping_add(int32_t a, int32_t b) {
	return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

/// @brief @p a − @p b, wrapping modulo 2^32 on overflow.
/// @param a The minuend, a raw word.
/// @param b The subtrahend, a raw word of the same format.
/// @return The difference, in their format.
[[nodiscard]] [[gnu::always_inline]] constexpr int32_t wrapping_sub(int32_t a, int32_t b) {
	return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

} // namespace deluge::dsp
