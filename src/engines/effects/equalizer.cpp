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

#include "engines/effects/kernels.h"
#include "util/fixedpoint.h"

namespace deluge::effects {

void Equalizer::process(std::span<dsp::StereoSample> block, const EqualizerParams& params) {
	const bool doBass = params.bass != 0;
	const bool doTreble = params.treble != 0;
	if (!doBass && !doTreble) {
		return;
	}

	// Bass. No-change represented by 0. Off completely represented by -536870912
	int32_t positive = (params.bass >> 1) + 1073741824;
	const int32_t bassAmount = (multiply_32x32_rshift32_rounded(positive, positive) << 1) - 536870912;

	// Treble. No-change represented by 536870912
	positive = (params.treble >> 1) + 1073741824;
	const int32_t trebleAmount = multiply_32x32_rshift32_rounded(positive, positive) << 1;

	if (doBass) {
		bass_coefficient_ = exp_of(120000000, (params.bass_frequency >> 5) * 6);
	}
	if (doTreble) {
		treble_coefficient_ = exp_of(700000000, (params.treble_frequency >> 5) * 6);
	}

	for (dsp::StereoSample& sample : block) {
		int32_t trebleOnlyL;
		int32_t trebleOnlyR;

		if (doTreble) {
			int32_t distanceToGoL = sample.l - without_treble_.l;
			int32_t distanceToGoR = sample.r - without_treble_.r;
			without_treble_.l += multiply_32x32_rshift32(distanceToGoL, treble_coefficient_) << 1;
			without_treble_.r += multiply_32x32_rshift32(distanceToGoR, treble_coefficient_) << 1;
			trebleOnlyL = sample.l - without_treble_.l;
			trebleOnlyR = sample.r - without_treble_.r;
			sample.l = without_treble_.l; // the treble is taken out, and added back scaled below
			sample.r = without_treble_.r;
		}

		if (doBass) {
			int32_t distanceToGoL = sample.l - bass_only_.l;
			int32_t distanceToGoR = sample.r - bass_only_.r;
			bass_only_.l += multiply_32x32_rshift32(distanceToGoL, bass_coefficient_);
			bass_only_.r += multiply_32x32_rshift32(distanceToGoR, bass_coefficient_);
		}

		if (doTreble) {
			sample.l += (multiply_32x32_rshift32(trebleOnlyL, trebleAmount) << 3);
			sample.r += (multiply_32x32_rshift32(trebleOnlyR, trebleAmount) << 3);
		}
		if (doBass) {
			sample.l += (multiply_32x32_rshift32(bass_only_.l, bassAmount) << 3);
			sample.r += (multiply_32x32_rshift32(bass_only_.r, bassAmount) << 3);
		}
	}
}

} // namespace deluge::effects
