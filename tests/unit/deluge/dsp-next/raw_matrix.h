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

// The matrix's per-cable helpers on raw words, for the models that work in the legacy patcher's raw words: a source
// value is Q31, a depth Q30, a combination and a range Q29, a neutral read at Q31.

#include "synth/modulation/cable_depth.h"
#include "util/param_functions.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::synth::test::raw_matrix {

[[nodiscard]] inline int32_t to_polarity(PatchSource from, CablePolarity polarity, int32_t value) {
	return synth::to_polarity(from, polarity, dsp::Q<31>::from_raw(value)).raw();
}
[[nodiscard]] inline int32_t apply_range(int32_t value, int32_t range) {
	return synth::apply_range(dsp::Q<29>::from_raw(value), dsp::Q<29>::from_raw(range)).raw();
}
[[nodiscard]] inline int32_t modified_depth(int32_t depth, PatchSource from, int32_t p) {
	return synth::modified_depth(dsp::Q<30>::from_raw(depth), from, p).raw();
}
[[nodiscard]] inline int32_t final_value_exp(int32_t neutral, int32_t combination, int32_t p) {
	return getFinalParameterValueExpWithDumbEnvelopeHack(dsp::Q<31>::from_raw(neutral),
	                                                     dsp::Q<29>::from_raw(combination), p);
}

/// The raw words as format @p T: a case's presets (Q31) or depths (Q30) for a MatrixBlock.
template <class T, std::size_t N>
[[nodiscard]] std::array<T, N> typed_words(const std::array<int32_t, N>& words) {
	std::array<T, N> out{};
	for (std::size_t i = 0; i < N; ++i) {
		out[i] = T::from_raw(words[i]);
	}
	return out;
}

} // namespace deluge::synth::test::raw_matrix
