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

#include "dsp/core/fixed_point.h"

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief How a sample's read heads mix a frame into a voice's buffer (R7 to R9, W1 of
/// docs/dev/time_stretcher_contracts.md).

/// @brief @p mix plus @p sample times @p amplitude, the rounded high word, summed wrapping. A sample voice's buffer
/// holds its mix at half scale: the Q30 products are the words of the voice's Q31 samples.
[[nodiscard, gnu::always_inline]] inline Q<31> mixed_in(Q<31> mix, Q<31> sample, Q<31> amplitude) {
	// A reinterpretation: the mix's words are read as Q30 so the Q31 product lands at half scale
	return reinterpret_bits<Q<31>>(mla_r(reinterpret_bits<Q<30>>(mix), sample, amplitude));
}

} // namespace deluge::dsp::fixed::sample_read
