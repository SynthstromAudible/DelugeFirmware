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
#include "dsp-next/fixed/wavetable/wave_table_data.h"           // fixed::WaveTableData (storage-independent band view)
#include "dsp-next/fixed/wavetable/wavetable_oscillator.h"      // fixed::WavetableOscillator (non-sync)
#include "dsp-next/fixed/wavetable/wavetable_sync_oscillator.h" // fixed::WavetableSyncOscillator (+ fixed::Resetter)
#include "synth/source/oscillator_source.h" // synth::OscillatorSource (+ Source, SourceRenderContext, BlockControls)
#include <cstdint>

namespace deluge::dsp {

/// @brief The wavetable source: a persistent per-voice-source @ref synth::Source over a storage-independent
/// band-data view.
///
/// It renders through the dsp-next view-based @ref deluge::dsp::fixed::WavetableOscillator (non-sync) /
/// @ref deluge::dsp::fixed::WavetableSyncOscillator (hard-sync), built from a @ref fixed::WaveTableData the app
/// supplies per block. It owns the wavetable's render phase across blocks (replacing @c oscPos for wavetable
/// sources, as @ref synth::VirtualAnalogSource did for the VA waves), the pulse-width-as-sync preamble, the `<<3`
/// amplitude VCA, and the phase-persistence rule. The storage `WaveTable` (SD loading / `AudioFile` refcounting /
/// stealable memory) stays app-side and builds the view (`WaveTable::view()`) — the source no longer reaches it,
/// so it is App-free and library-resident (the band-data port, `docs/dev/synth_source_ports.md`).
class WavetableSource : public synth::OscillatorSource {
	Phasor phasor_;              ///< the original-domain render phase, persistent across blocks
	fixed::WaveTableData table_; ///< the band-data view (default = null/empty); set per block by the app edge
	int32_t wave_index_last_ = 0;
	int32_t wave_index_increment_ = 0;

public:
	/// @param seed the engine's phase state (increment set per block via @ref setIncrement).
	explicit WavetableSource(Phasor seed) : phasor_{seed} {}

	/// The current render phase (UQ0.32). Persists across blocks — Voice reads it for osc-sync / skip.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	/// Jump the phase (a skipped source still advances, or an external reseed). Leaves the increment.
	void seek(uint32_t phase) { phasor_.seek(phase); }
	/// Set the per-block pitch (UQ0.32). Voice calls this each block before @ref renderBlock.
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }

	/// The wavetable band-data **view** to render (built app-side from the source's loaded `WaveTable`) — set per
	/// block by Voice. The storage seam is now this view, not a `WaveTable` pointer, so the source is App-free.
	void setWaveData(const fixed::WaveTableData& data) { table_ = data; }
	/// The per-block wave-index (morph) inputs: @p last_index is the previous block's index, @p increment its
	/// per-sample slope. The engine applies the legacy `+2^30` centering itself.
	void setWaveIndex(int32_t last_index, int32_t increment) {
		wave_index_last_ = last_index;
		wave_index_increment_ = increment;
	}

	void renderBlock(const synth::BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch);

	/// Render one block into @p output (sets the wavetable + per-block pitch + wave-index/morph inputs first), VCA
	/// per @p controls. The public block-render API used by @ref RingModEngine (raw render) and by
	/// @ref renderOscBlock. The engine owns its phase across blocks.
	void renderInto(const synth::BlockControls& controls, const fixed::WaveTableData& table, int32_t lastWaveIndex,
	                int32_t waveIndexIncrement, uint32_t phaseIncrement, fixed::Buffer output, fixed::Buffer scratch) {
		setWaveData(table);
		setIncrement(phaseIncrement);
		setWaveIndex(lastWaveIndex, waveIndexIncrement);
		renderBlock(controls, output, scratch);
	}

protected:
	void renderOscBlock(const synth::BlockControls& controls, const synth::SourceRenderContext& ctx,
	                    fixed::Buffer out) override {
		renderInto(controls, ctx.waveTableData, ctx.waveIndexLast, ctx.waveIndexIncrement, ctx.phaseIncrement, out,
		           fixed::Buffer{ctx.syncScratch, static_cast<size_t>(ctx.numSamples)});
	}
};

} // namespace deluge::dsp
