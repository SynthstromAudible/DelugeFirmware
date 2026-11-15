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

// The param curves on raw words, for the references and models that work in the legacy app's raw words: each reads
// its neutral at Q31 (the curves' arithmetic is the same for every format) and its combination at Q29.

#include "dsp-next/fixed/curves/exp_curve.h"
#include "dsp-next/fixed/curves/instant_tan.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include <cstdint>

namespace deluge::dsp::test::raw_curves {

[[nodiscard]] inline int32_t curve_square(int32_t neutral, int32_t combination) {
	return fixed::curve_square(Q<31>::from_raw(neutral), Q<29>::from_raw(combination)).raw();
}
[[nodiscard]] inline int32_t curve_linear(int32_t neutral, int32_t combination) {
	return fixed::curve_linear(Q<31>::from_raw(neutral), Q<29>::from_raw(combination)).raw();
}
[[nodiscard]] inline int32_t curve_bipolar(int32_t neutral, int32_t combination) {
	return fixed::curve_bipolar(Q<31>::from_raw(neutral), Q<29>::from_raw(combination)).raw();
}
[[nodiscard]] inline int32_t curve_exponential(int32_t neutral, int32_t combination) {
	return fixed::curve_exponential(Q<31>::from_raw(neutral), Q<29>::from_raw(combination)).raw();
}
[[nodiscard]] inline int32_t release_rate(int32_t combination) {
	return fixed::release_rate(Q<29>::from_raw(combination)).raw();
}
/// The preset read at Q31, the adjustment as Q26 octaves.
[[nodiscard]] inline int32_t exp_curve(int32_t preset, int32_t adjustment) {
	return fixed::exp_curve(Q<31>::from_raw(preset), Q<26>::from_raw(adjustment)).raw();
}
/// The angle Q31, the tangent Q28.
[[nodiscard]] inline int32_t instant_tan(int32_t angle) {
	return fixed::instant_tan(Q<31>::from_raw(angle)).raw();
}

} // namespace deluge::dsp::test::raw_curves
