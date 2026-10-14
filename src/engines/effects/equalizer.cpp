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

#include "engines/effects/equalizer.h"

#include "dsp-next/fixed/curves/exp_curve.h"
#include "util/fixedpoint.h"

namespace deluge::effects {

void Equalizer::process(std::span<dsp::StereoSample> block, const EqualizerParams& params) {
	const bool do_bass = params.bass != 0;
	const bool do_treble = params.treble != 0;
	if (!do_bass && !do_treble) {
		return;
	}

	// Each band's gain, from its knob squared: bass 0 is flat and -536870912 removes it; treble 536870912 is flat.
	int32_t positive = (params.bass >> 1) + 1073741824;
	const int32_t bass_gain = (multiply_32x32_rshift32_rounded(positive, positive) << 1) - 536870912;
	positive = (params.treble >> 1) + 1073741824;
	const int32_t treble_gain = multiply_32x32_rshift32_rounded(positive, positive) << 1;

	if (do_bass) {
		bass_coefficient_ = dsp::fixed::exp_curve(120000000, (params.bass_frequency >> 5) * 6);
	}
	if (do_treble) {
		treble_coefficient_ = dsp::fixed::exp_curve(700000000, (params.treble_frequency >> 5) * 6);
	}

	for (dsp::StereoSample& sample : block) {
		// The treble is split off (a one-pole low-pass keeps the rest), then added back scaled; the bass is a one-pole
		// low-pass of what remains, added on top scaled.
		int32_t treble_l = 0;
		int32_t treble_r = 0;
		if (do_treble) {
			without_treble_.l += multiply_32x32_rshift32(sample.l - without_treble_.l, treble_coefficient_) << 1;
			without_treble_.r += multiply_32x32_rshift32(sample.r - without_treble_.r, treble_coefficient_) << 1;
			treble_l = sample.l - without_treble_.l;
			treble_r = sample.r - without_treble_.r;
			sample.l = without_treble_.l;
			sample.r = without_treble_.r;
		}
		if (do_bass) {
			bass_only_.l += multiply_32x32_rshift32(sample.l - bass_only_.l, bass_coefficient_);
			bass_only_.r += multiply_32x32_rshift32(sample.r - bass_only_.r, bass_coefficient_);
		}
		if (do_treble) {
			sample.l += multiply_32x32_rshift32(treble_l, treble_gain) << 3;
			sample.r += multiply_32x32_rshift32(treble_r, treble_gain) << 3;
		}
		if (do_bass) {
			sample.l += multiply_32x32_rshift32(bass_only_.l, bass_gain) << 3;
			sample.r += multiply_32x32_rshift32(bass_only_.r, bass_gain) << 3;
		}
	}
}

} // namespace deluge::effects
