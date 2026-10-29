/*
 * Copyright © 2026 Mark Adams
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

#include "dsp-next/fixed/osc/supersaw.h"    // deluge::dsp::fixed::Supersaw
#include "synth/source/oscillator_source.h" // OscillatorSource (+ Source, SourceRenderContext, BlockControls)
#include <cstdint>

namespace deluge::synth {

/// @brief The supersaw @ref Source: the dsp-next @ref deluge::dsp::fixed::Supersaw kernel and its seven saw phases,
/// which persist across blocks. The wave index sets the spread and pulse width the number of saws. It doesn't do
/// osc sync, as a slave; as a sync master its phase is the centre saw's.
class SupersawSource : public OscillatorSource {
public:
	using SidePhases = deluge::dsp::fixed::Supersaw::SidePhases;

	/// @param centrePhase the centre saw's phase (UQ0.32), which is the source's phase
	/// @param sidePhases the six side saws' phases
	SupersawSource(uint32_t centrePhase, const SidePhases& sidePhases)
	    : centre_phase_{centrePhase}, side_phases_{sidePhases} {}

	/// The centre saw's phase (UQ0.32). Voice reads it to seed osc sync from this source as master.
	[[nodiscard]] uint32_t phase() const { return centre_phase_; }

	/// Jump the centre saw's phase, moving the side saws by the same amount so they keep their offsets (a skipped
	/// osc-sync master still progresses).
	void seek(uint32_t phase) {
		const uint32_t delta = phase - centre_phase_;
		centre_phase_ = phase;
		for (uint32_t& side : side_phases_) {
			side += delta;
		}
	}

	/// Render one block into @p output, VCA per @p controls. The public block-render API used by @ref RingModEngine
	/// (raw render) and by @ref renderOscBlock.
	/// @param waveIndexLast the wave index param's previous block value, which sets the spread
	/// @param waveIndexIncrement its per-sample slope; spread is taken where the block ends
	void renderInto(const BlockControls& controls, uint32_t phaseIncrement, int32_t waveIndexLast,
	                int32_t waveIndexIncrement, deluge::dsp::fixed::Buffer output, deluge::dsp::fixed::Buffer scratch);

protected:
	void renderOscBlock(const BlockControls& controls, const SourceRenderContext& ctx,
	                    deluge::dsp::fixed::Buffer out) override {
		renderInto(controls, ctx.phaseIncrement, ctx.waveIndexLast, ctx.waveIndexIncrement, out,
		           deluge::dsp::fixed::Buffer{ctx.syncScratch, static_cast<size_t>(ctx.numSamples)});
	}

private:
	uint32_t centre_phase_;
	SidePhases side_phases_;
};

} // namespace deluge::synth
