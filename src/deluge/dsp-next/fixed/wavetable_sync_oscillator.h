/*
 * Copyright © 2020-2026 Synthstrom Audible Limited
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

#include "dsp-next/core/generator.h" // BlockGenerator, Buffer, q31
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/sinc_kernel.h"
#include "dsp-next/fixed/sync_windower.h" // Resetter, SyncWindower
#include "dsp-next/fixed/wave_table_data.h"
#include "dsp-next/fixed/wavetable_cycle.h"
#include "dsp-next/fixed/wavetable_cycle_chunker.h"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::dsp::fixed {

/// @brief A hard-synced wavetable oscillator, as a dsp-next component.
///
/// Like @ref WavetableOscillator, but each piece is restarted at every @ref Resetter crossover
/// (classic osc-sync) by a @ref SyncWindower, which owns the window split + half-sine seam blend.
/// The same @ref WavetableCycleChunker drives the cross-cycle morph; the only difference is that
/// each chunk is filled through a windower rather than straight.
///
/// The morph is indexed by output position — `crossCycleStrength2 + window.offset · increment` —
/// which is identically the legacy per-crossover callback accumulation (Σ increment·(samples−1) =
/// increment·offset), so the redone crossover sample reuses the same morph and the result is
/// bit-exact.
class [[gnu::hot]] WavetableSyncOscillator : public BlockGenerator<q31> {
public:
	WavetableSyncOscillator(WaveTableData table, uint32_t phase, uint32_t increment, Resetter resetter,
	                        uint32_t retrigger_phase, int32_t wave_index, int32_t wave_index_increment)
	    : table_{table}, phasor_{phase, increment}, resetter_{resetter}, retrigger_phase_{retrigger_phase},
	      wave_index_{wave_index}, wave_index_increment_{wave_index_increment} {}

	/// The audible phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		uint32_t phase = phasor_.phase();
		uint32_t phase_increment = phasor_.increment();
		uint32_t resetter_phase = resetter_.phase;
		uint32_t resetter_increment = resetter_.phase_increment;

		if (table_.num_cycles() > 1) [[likely]] {
			WavetableCycleChunker chunker{table_, output, phase_increment, wave_index_, wave_index_increment_};
			while (std::optional<CycleChunk> chunk = chunker.next()) {
				if (chunk->band != nullptr) {
					const int16_t* kernel = select_sinc_kernel(phase_increment, chunk->band->maxPhaseIncrement);
					phase =
					    renderSyncedChunk(*chunk, phase, phase_increment, resetter_phase, resetter_increment, kernel);
				}
				// The resetter advances past the whole chunk whether or not it had data.
				resetter_phase += resetter_increment * static_cast<uint32_t>(chunk->output.size());
			}
		}
		else {
			const WaveTableBand& band = table_.band(table_.select_band(phase_increment));
			const int16_t* kernel = select_sinc_kernel(phase_increment, band.maxPhaseIncrement);
			SyncWindower windower{output, Resetter{resetter_phase, resetter_increment}, phase, phase_increment,
			                      retrigger_phase_};
			while (std::optional<SyncWindower::Window> window = windower.next()) {
				render_wavetable_single_cycle(window->output, band, window->phasor, kernel);
			}
			phase = windower.phase();
		}

		phasor_.seek(phase);
	}

private:
	/// Fill one morph chunk through a sync windower, restarting at each crossover. Returns the audible
	/// phase after the chunk.
	[[gnu::always_inline]] uint32_t renderSyncedChunk(const CycleChunk& chunk, uint32_t phase, uint32_t phase_increment,
	                                                  uint32_t resetter_phase, uint32_t resetter_increment,
	                                                  const int16_t* kernel) {
		SyncWindower windower{chunk.output, Resetter{resetter_phase, resetter_increment}, phase, phase_increment,
		                      retrigger_phase_};
		while (std::optional<SyncWindower::Window> window = windower.next()) {
			// Morph by output position: the same value the legacy per-crossover callback built up.
			uint32_t cross_cycle_blend =
			    chunk.cross_cycle_blend
			    + static_cast<uint32_t>(chunk.cross_cycle_blend_increment) * static_cast<uint32_t>(window->offset);
			render_wavetable_cycle(window->output, chunk.first_cycle, *chunk.band, window->phasor, cross_cycle_blend,
			                       chunk.cross_cycle_blend_increment, kernel);
		}
		return windower.phase();
	}

	WaveTableData table_;
	Phasor phasor_;
	Resetter resetter_;
	uint32_t retrigger_phase_;
	int32_t wave_index_;
	int32_t wave_index_increment_;
};

} // namespace deluge::dsp::fixed
