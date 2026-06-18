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
#include "synth/source/source.h"  // Source + BlockControls
#include "wavetable_oscillator.h" // WavetableOscillator (+ storage/wave_table.h)
#include <cstdint>

namespace deluge::dsp {

/// @brief The wavetable source engine: a persistent per-voice-source @ref synth::Source wrapping the
/// sample-backed @ref WavetableOscillator.
///
/// Unlike the virtual-analog waves, the wavetable loads its band data from SD, so it depends on `storage/` and
/// cannot live in the freestanding synth/ library — it stays **app-side** as the port-boundary adapter (the same
/// shape the plan prescribes for SAMPLE: "wrap the existing path in place"). It owns the wavetable's render phase
/// across blocks (replacing @c oscPos for wavetable sources, as @ref synth::VirtualAnalogEngine did for the VA
/// waves), the pulse-width-as-sync preamble, the per-block @ref WavetableOscillator, the `<<3` amplitude VCA, and
/// the phase-persistence rule.
///
/// The storage dependency is deliberately confined to the single @ref table_ member + @ref setWaveTable, so the
/// eventual dsp-next storage port (`WaveTable::render` → a band-data view) swaps one seam without touching this
/// engine's logic — at which point the engine could itself move into the library.
///
/// @note Per the L1 reframe this is a **source component**, not a swappable engine — see
/// `docs/dev/synth_l1_engine_architecture.md`. The `*Engine` name predates that doc; the rename rides the
/// engine-forming slices.
class WavetableEngine : public synth::Source {
	Phasor phasor_; ///< the original-domain render phase, persistent across blocks
	WaveTable* table_ = nullptr;
	int32_t wave_index_last_ = 0;
	int32_t wave_index_increment_ = 0;

public:
	/// @param seed the engine's phase state (increment set per block via @ref setIncrement).
	explicit WavetableEngine(Phasor seed) : phasor_{seed} {}

	/// The current render phase (UQ0.32). Persists across blocks — Voice reads it for osc-sync / skip.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	/// Jump the phase (a skipped source still advances, or an external reseed). Leaves the increment.
	void seek(uint32_t phase) { phasor_.seek(phase); }
	/// Set the per-block pitch (UQ0.32). Voice calls this each block before @ref renderBlock.
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }

	/// The wavetable to render (from the source's loaded `AudioFile`) — set per block by Voice. The only storage
	/// seam.
	void setWaveTable(WaveTable* table) { table_ = table; }
	/// The per-block wave-index (morph) inputs: @p last_index is the previous block's index, @p increment its
	/// per-sample slope. The engine applies the legacy `+2^30` centering itself.
	void setWaveIndex(int32_t last_index, int32_t increment) {
		wave_index_last_ = last_index;
		wave_index_increment_ = increment;
	}

	void renderBlock(const synth::BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) override;

	/// Render one block as a @ref synth::Source, reporting the per-source lifecycle outcome. A wavetable source
	/// never dies, so this is always ACTIVE; the uniform return type is the seam the per-source dispatch folds
	/// onto. Sets the wavetable + per-block pitch + wave-index (morph) inputs, then renders into @p output.
	synth::SourceRenderResult renderSource(const synth::BlockControls& controls, WaveTable* table,
	                                       int32_t lastWaveIndex, int32_t waveIndexIncrement, uint32_t phaseIncrement,
	                                       fixed::Buffer output, fixed::Buffer scratch) {
		setWaveTable(table);
		setIncrement(phaseIncrement);
		setWaveIndex(lastWaveIndex, waveIndexIncrement);
		renderBlock(controls, output, scratch);
		return synth::SourceRenderResult::ACTIVE;
	}
};

} // namespace deluge::dsp
