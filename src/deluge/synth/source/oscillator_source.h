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

#include "dsp-next/core/types.h" // deluge::dsp::fixed::Buffer
#include "synth/source/source.h" // Source, SourceRenderContext, BlockControls
#include "util/fixedpoint.h"     // multiply_32x32_rshift32
#include <cstdint>
#include <cstring>

namespace deluge::synth {

/// @brief Shared base for the oscillator-family @ref Source%s (virtual-analog, wavetable). Both render a mono block
/// (VCA applied per @ref BlockControls) into a work buffer and pan-fold it into the voice's stereo buffer, with the
/// same osc-sync freq-too-high SKIP. The *only* per-engine difference — setting the engine's per-block inputs and
/// running the block render — is the @ref renderOscBlock hook each concrete engine implements.
class OscillatorSource : public Source {
public:
	SourceRenderResult renderSource(const SourceRenderContext& ctx) final {
		// Osc sync with the freq too high: skip this unison part without unassigning.
		if (ctx.doOscSync && !ctx.oscSyncPhaseIncrement) {
			return SourceRenderResult::SKIP;
		}

		// Stereo voices render mono into a scratch buffer, then pan-fold; mono renders straight into oscBuffer.
		int32_t* renderBuffer = ctx.oscBuffer;
		if (ctx.stereoBuffer) {
			renderBuffer = ctx.stereoScratch;
			memset(renderBuffer, 0, ctx.stereoScratchClearSamples * sizeof(int32_t));
		}

		renderOscBlock(ctx.controls, ctx,
		               deluge::dsp::fixed::Buffer{renderBuffer, static_cast<size_t>(ctx.numSamples)});

		if (ctx.stereoBuffer) {
			// TODO: if the render buffer were typed we could use addPannedMono()
			for (int32_t i = 0; i < ctx.numSamples; i++) {
				ctx.oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[i], ctx.amplitudeL) << 2;
				ctx.oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[i], ctx.amplitudeR) << 2;
			}
		}
		return SourceRenderResult::ACTIVE;
	}

protected:
	/// Set the engine's per-block inputs (pitch — and, for the wavetable, its table + morph index) and render the
	/// mono block into @p out. The oscillator hard-sync work buffer is @c ctx.syncScratch.
	virtual void renderOscBlock(const BlockControls& controls, const SourceRenderContext& ctx,
	                            deluge::dsp::fixed::Buffer out) = 0;
};

} // namespace deluge::synth
