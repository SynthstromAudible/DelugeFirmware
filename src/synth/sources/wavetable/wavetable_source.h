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

#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/wavetable/wave_table_data.h" // deluge::dsp::fixed::WaveTableData (storage-independent band view)
#include "dsp-next/fixed/wavetable/wavetable_oscillator.h" // deluge::dsp::fixed::WavetableOscillator (non-sync)
#include "dsp-next/fixed/wavetable/wavetable_sync_oscillator.h" // deluge::dsp::fixed::WavetableSyncOscillator (+ deluge::dsp::fixed::Resetter)
#include "synth/sources/oscillator_source.h" // OscillatorSource (+ Source, SourceBlock, BlockControls)
#include <cstdint>

namespace deluge::synth {

/// @brief The wavetable source: a persistent per-voice-source @ref synth::Source over a storage-independent
/// band-data view.
///
/// It renders through the dsp-next view-based @ref deluge::dsp::fixed::WavetableOscillator (non-sync) /
/// @ref deluge::dsp::fixed::WavetableSyncOscillator (hard-sync), built from a @ref deluge::dsp::fixed::WaveTableData
/// the app supplies per block. It owns the wavetable's render phase across blocks (replacing @c oscPos for wavetable
/// sources, as @ref synth::VirtualAnalogSource did for the VA waves), the pulse-width-as-sync preamble, the `<<3`
/// amplitude VCA, and the phase-persistence rule. The storage `WaveTable` (SD loading / `AudioFile` refcounting /
/// stealable memory) stays app-side and builds the view (`WaveTable::view()`) — the source no longer reaches it,
/// so it is App-free and library-resident (the band-data port, `docs/dev/synth_source_ports.md`).
class WavetableSource : public OscillatorSource {
	deluge::dsp::Phasor phasor_; ///< the original-domain render phase, persistent across blocks
	deluge::dsp::fixed::WaveTableData
	    table_; ///< the band-data view (default = null/empty); set per block by the app edge
	int32_t wave_index_last_ = 0;
	int32_t wave_index_increment_ = 0;

public:
	/// @param seed the engine's phase state (increment set per block via @ref set_increment).
	explicit WavetableSource(deluge::dsp::Phasor seed) : phasor_{seed} {}

	/// The current render phase. Persists across blocks — Voice reads it for osc-sync / skip.
	[[nodiscard]] dsp::UQ<32> phase() const { return phasor_.phase(); }
	[[nodiscard]] std::optional<dsp::UQ<32>> current_phase() const override { return phase(); }
	void advance_phase(dsp::UQ<32> delta) override { seek(add_wrap(phase(), delta)); }

	/// Jump the phase (a skipped source still advances, or an external reseed). Leaves the increment.
	void seek(dsp::UQ<32> phase) { phasor_.seek(phase); }
	/// Set the per-block pitch. Voice calls this each block before @ref render_block.
	void set_increment(dsp::UQ<32> increment) { phasor_.set_increment(increment); }

	/// The wavetable band-data **view** to render (built app-side from the source's loaded `WaveTable`) — set per
	/// block by Voice. The storage seam is now this view, not a `WaveTable` pointer, so the source is App-free.
	void set_wave_data(const deluge::dsp::fixed::WaveTableData& data) { table_ = data; }
	/// The per-block wave-index (morph) inputs: @p last_index is the previous block's index, @p increment its
	/// per-sample slope. The engine applies the legacy `+2^30` centering itself.
	void set_wave_index(int32_t last_index, int32_t increment) {
		wave_index_last_ = last_index;
		wave_index_increment_ = increment;
	}

	void render_block(const BlockControls& controls, std::span<dsp::Q<31>> output, std::span<dsp::Q<31>> scratch);

	/// Render one block into @p output (sets the wavetable + per-block pitch + wave-index/morph inputs first), VCA
	/// per @p controls. The public block-render API used by @ref RingModEngine (raw render) and by
	/// @ref renderOscBlock. The engine owns its phase across blocks.
	void render_into(const BlockControls& controls, const deluge::dsp::fixed::WaveTableData& table,
	                 int32_t last_wave_index, int32_t wave_index_increment, dsp::UQ<32> phase_increment,
	                 std::span<dsp::Q<31>> output, std::span<dsp::Q<31>> scratch) {
		set_wave_data(table);
		set_increment(phase_increment);
		set_wave_index(last_wave_index, wave_index_increment);
		render_block(controls, output, scratch);
	}

protected:
	void render_osc_block(const BlockControls& controls, const SourceBlock& block, std::span<dsp::Q<31>> out) override {
		render_into(controls, block.wavetable.data, block.wavetable.index_last, block.wavetable.index_increment,
		            block.phase_increment, out,
		            std::span<dsp::Q<31>>{block.sync_scratch, static_cast<size_t>(block.num_samples)});
	}
};

} // namespace deluge::synth
