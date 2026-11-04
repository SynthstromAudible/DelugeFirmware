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

#include "dsp-next/core/types.h"  // std::span<dsp::Q<31>>
#include "synth/sources/source.h" // Source, SourceBlock, BlockControls
#include <algorithm>              // std::fill_n
#include <cstdint>

namespace deluge::synth {

/// @brief Shared base for the oscillator-family @ref Source%s (virtual-analog, wavetable). Both render a mono block
/// (VCA applied per @ref BlockControls) into a work buffer and pan-fold it into the voice's stereo buffer, with the
/// same osc-sync freq-too-high SKIP. The *only* per-engine difference — setting the engine's per-block inputs and
/// running the block render — is the @ref renderOscBlock hook each concrete engine implements.
class OscillatorSource : public Source {
public:
	SourceRenderResult render(const SourceBlock& block) final {
		// Osc sync with the master too high to render: skip this unison part without unassigning.
		if (block.sync.has_value() && block.sync->increment == 0u) {
			return SourceRenderResult::SKIP;
		}

		// Stereo voices render mono into a scratch buffer, then pan-fold; mono renders straight into oscBuffer.
		dsp::Q<31>* render_buffer = block.osc_buffer;
		if (block.stereo_buffer) {
			render_buffer = block.stereo_scratch;
			std::fill_n(render_buffer, block.stereo_scratch_clear_samples, dsp::Q<31>{});
		}

		render_osc_block(block.controls, block,
		                 std::span<dsp::Q<31>>{render_buffer, static_cast<size_t>(block.num_samples)});

		if (block.stereo_buffer) {
			for (int32_t i = 0; i < block.num_samples; i++) {
				add_panned(&block.osc_buffer[i << 1], render_buffer[i], block.amplitude_l, block.amplitude_r);
			}
		}
		return SourceRenderResult::ACTIVE;
	}

protected:
	/// Set the engine's per-block inputs (pitch — and, for the wavetable, its table + morph index) and render the
	/// mono block into @p out. The oscillator hard-sync work buffer is @c block.sync_scratch.
	virtual void render_osc_block(const BlockControls& controls, const SourceBlock& block,
	                              std::span<dsp::Q<31>> out) = 0;
};

} // namespace deluge::synth
