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

// The shaping curves on raw Q31 words, for the references, which stay raw: each converts at entry and exit.

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <cstdint>

namespace raw_shaping {

/// @brief fixed::tanh<Drive> on a raw Q31 word.
template <uint32_t Drive>
[[nodiscard]] inline int32_t tanh(int32_t x) {
	return deluge::dsp::fixed::tanh<Drive>(deluge::dsp::Q<31>::from_raw(x)).raw();
}

/// @brief The run-time-drive fixed::tanh on a raw Q31 word.
[[nodiscard]] inline int32_t tanh(int32_t x, uint32_t drive) {
	return deluge::dsp::fixed::tanh(deluge::dsp::Q<31>::from_raw(x), drive).raw();
}

/// @brief AntialiasedTanh's step on a raw Q31 word.
[[nodiscard]] inline int32_t antialiased_tanh(const deluge::dsp::fixed::AntialiasedTanh::Coefficients& c,
                                              deluge::dsp::fixed::AntialiasedTanh::State& s, int32_t x) {
	return deluge::dsp::fixed::AntialiasedTanh::step(c, deluge::dsp::EmptyBlock{}, s, deluge::dsp::Q<31>::from_raw(x))
	    .raw();
}

/// @brief AntialiasedTanh's seed from a raw Q31 word.
inline void antialiased_tanh_seed(const deluge::dsp::fixed::AntialiasedTanh::Coefficients& c,
                                  deluge::dsp::fixed::AntialiasedTanh::State& s, int32_t x) {
	deluge::dsp::fixed::AntialiasedTanh::seed(c, s, deluge::dsp::Q<31>::from_raw(x));
}

} // namespace raw_shaping
