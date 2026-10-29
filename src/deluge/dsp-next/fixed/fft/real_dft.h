/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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
#include <span>

namespace deluge::dsp::fixed::fft {

/// @brief The 0…N/2 bins of an N-point real DFT by table sine and cosine, for a cycle whose length is not a power of
///        two. Each bin is the Q31 product sum doubled once, as the band builder expects of a forward transform.
/// @param in  The N samples.
/// @param out At least N/2 + 1 bins.
void real_dft(std::span<const int32_t> in, std::span<Complex> out);

} // namespace deluge::dsp::fixed::fft
