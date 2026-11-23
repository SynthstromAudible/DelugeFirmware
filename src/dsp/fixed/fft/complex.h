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
#include <type_traits>

namespace deluge::dsp::fixed::fft {

/// @brief One complex value of the int32 transforms: real then imaginary, two adjacent words, so a span of them is the
///        interleaved (r, i) layout the radix kernels load and store.
struct Complex {
	int32_t r;
	int32_t i;
};
static_assert(sizeof(Complex) == 2 * sizeof(int32_t) && std::is_trivially_copyable_v<Complex>);

} // namespace deluge::dsp::fixed::fft
