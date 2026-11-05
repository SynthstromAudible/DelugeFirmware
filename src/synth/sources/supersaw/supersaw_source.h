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

#include "dsp-next/fixed/osc/supersaw.h"     // deluge::dsp::fixed::Supersaw
#include "synth/sources/oscillator_source.h" // OscillatorSource (+ Source, SourceBlock, BlockControls)
#include <cstdint>

namespace deluge::synth {

/// @brief The supersaw @ref Source: the dsp-next @ref deluge::dsp::fixed::Supersaw kernel and its seven saw phases,
/// which persist across blocks. The wave index sets the spread and pulse width the number of saws. It doesn't do
/// osc sync, as a slave; as a sync master its phase is the centre saw's.
class SupersawSource : public OscillatorSource {
public:
	using SidePhases = deluge::dsp::fixed::Supersaw::SidePhases;

	/// @param centre_phase the centre saw's phase, which is the source's phase
	/// @param side_phases the six side saws' phases
	/// The phases are held as words: dsp::fixed::Supersaw steps them by reference
	SupersawSource(dsp::UQ<32> centre_phase, const SidePhases& side_phases)
	    : centre_phase_{centre_phase.raw()}, side_phases_{side_phases} {}

	/// The centre saw's phase. Voice reads it to seed osc sync from this source as master.
	[[nodiscard]] dsp::UQ<32> phase() const { return dsp::UQ<32>::from_raw(centre_phase_); }

	[[nodiscard]] std::optional<dsp::UQ<32>> current_phase() const override { return phase(); }
	void advance_phase(dsp::UQ<32> delta) override { seek(add_wrap(phase(), delta)); }

	/// Jump the centre saw's phase, moving the side saws by the same amount so they keep their offsets (a skipped
	/// osc-sync master still progresses).
	void seek(dsp::UQ<32> phase) {
		const uint32_t delta = phase.raw() - centre_phase_;
		centre_phase_ = phase.raw();
		for (uint32_t& side : side_phases_) {
			side += delta;
		}
	}

	/// Render one block into @p output, VCA per @p controls. The public block-render API used by @ref RingModEngine
	/// (raw render) and by @ref renderOscBlock.
	/// @param waveIndexLast the wave index param's previous block value, which sets the spread
	/// @param waveIndexIncrement its per-sample slope; spread is taken where the block ends
	void render_into(const BlockControls& controls, dsp::UQ<32> phase_increment, int32_t wave_index_last,
	                 int32_t wave_index_increment, std::span<dsp::Q<31>> output, std::span<dsp::Q<31>> scratch);

protected:
	void render_osc_block(const BlockControls& controls, const SourceBlock& block, std::span<dsp::Q<31>> out) override {
		render_into(controls, block.phase_increment, block.wavetable.index_last, block.wavetable.index_increment, out,
		            std::span<dsp::Q<31>>{block.sync_scratch, static_cast<size_t>(block.num_samples)});
	}

private:
	// The saws' phases as the supersaw kernel steps them: raw words, one cycle at 2^32
	uint32_t centre_phase_;
	SidePhases side_phases_;
};

} // namespace deluge::synth
