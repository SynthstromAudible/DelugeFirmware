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
/// @brief Wrapping q31 addition and subtraction: two's complement, as the hardware's `add` and `sub` compute.
///
/// A signed overflow is undefined in C++, and once a kernel's step is inlined into a loop the compiler may assume it
/// never happens. These compute through uint32_t, whose arithmetic is defined modulo 2^32, so an out-of-range result
/// wraps exactly as the target's instruction does, and they compile to that one instruction.
namespace deluge::dsp {

/// @brief @p a + @p b, Q31, wrapping modulo 2^32 on overflow.
[[nodiscard]] [[gnu::always_inline]] constexpr q31 wrapping_add(q31 a, q31 b) {
	return static_cast<q31>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

/// @brief @p a − @p b, Q31, wrapping modulo 2^32 on overflow.
[[nodiscard]] [[gnu::always_inline]] constexpr q31 wrapping_sub(q31 a, q31 b) {
	return static_cast<q31>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

} // namespace deluge::dsp
