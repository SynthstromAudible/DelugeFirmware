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
#include "synth/source/dx7_source.h"
#include "util/fixedpoint.h" // multiply_32x32_rshift32
#include <span>

namespace deluge::synth {

SourceRenderResult DX7Source::renderSource(const SourceRenderContext& ctx) {
	// The port renders the FM block (pooled DxVoice + log-pitch/control derivation); the bespoke per-unison
	// amplitude+sum below is Voice's buffer layout. An empty span means the voice has ended.
	std::span<const int32_t> fm =
	    dx_->render(ctx.numSamples, ctx.phaseIncrement, ctx.ampModParam, ctx.patchChanged, ctx.midinote);
	if (fm.empty()) {
		return SourceRenderResult::UNASSIGN;
	}

	int32_t sourceAmplitudeNow = ctx.sourceAmplitude;
	if (ctx.stereoUnison) {
		for (int i = 0; i < ctx.numSamples; i++) {
			sourceAmplitudeNow += ctx.amplitudeIncrement;
			int amplified = multiply_32x32_rshift32(fm[i], sourceAmplitudeNow) << 6;
			ctx.oscBuffer[(i << 1)] += multiply_32x32_rshift32(amplified, ctx.amplitudeL) << 2;
			ctx.oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(amplified, ctx.amplitudeR) << 2;
		}
	}
	else {
		for (int i = 0; i < ctx.numSamples; i++) {
			sourceAmplitudeNow += ctx.amplitudeIncrement;
			ctx.oscBuffer[i] += multiply_32x32_rshift32(fm[i], sourceAmplitudeNow) << 6;
		}
	}
	return SourceRenderResult::ACTIVE;
}

} // namespace deluge::synth
