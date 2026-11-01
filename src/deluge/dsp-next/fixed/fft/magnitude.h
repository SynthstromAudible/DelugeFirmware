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

#include "dsp-next/fixed/fft/complex.h"

#include <cstdint>

namespace deluge::dsp::fixed::fft {

/// @brief An FFT bin's magnitude, √(r² + i²), approximated within about 0.15% for any inputs (INT32_MIN included).
/// @details Reads √(1 + q²) from a 257-entry table at q = min/max of the two absolute parts, rounded to 1/256, and
/// scales the larger part by it. The ratio is formed in 32 bits (the A9 has no 64-bit divide): the parts are first
/// shifted down so the larger fits in 23 bits.
/// @param bin The bin, real and imaginary parts in the transform's units.
/// @return The magnitude in the parts' units, saturated at INT32_MAX.
[[nodiscard]] int32_t magnitude(Complex bin);

} // namespace deluge::dsp::fixed::fft
