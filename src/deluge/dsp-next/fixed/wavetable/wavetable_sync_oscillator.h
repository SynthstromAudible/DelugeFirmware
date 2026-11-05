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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/sync/sync_windower.h" // Resetter, SyncWindower
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "dsp-next/fixed/wavetable/wavetable_cycle_chunker.h"
#include <cstdint>
#include <optional>
#include <type_traits>

namespace deluge::dsp::fixed {

/// @brief A hard-synced wavetable oscillator, as a dsp-next component.
///
/// Like @ref WavetableOscillator, but each @ref CycleChunk is rendered through a @ref SyncWindower,
/// which restarts the wave at every @ref Resetter crossover (classic osc-sync) and seam-blends each
/// restart. The chunk's cross-cycle morph is indexed by the window's offset — identically the legacy
/// per-crossover callback accumulation — so the redone crossover sample reuses the same blend and
/// the result is bit-exact. A plain @ref block_generator with no vptr: sync windows and chunks are
/// block-level, so there is no per-sample `render()`.
class WavetableSyncOscillator {
public:
	using value_type = Q<31>; ///< Wave samples.

	WavetableSyncOscillator(WaveTableData table, UQ<32> phase, UQ<32> increment, Resetter resetter,
	                        UQ<32> retrigger_phase, int32_t wave_index, int32_t wave_index_increment)
	    : table_{table}, phasor_{phase, increment}, resetter_{resetter}, retrigger_phase_{retrigger_phase},
	      wave_index_{wave_index}, wave_index_increment_{wave_index_increment} {}

	/// The audible phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] UQ<32> phase() const { return phasor_.phase(); }

	/// @brief Fill @p output with the next `output.size()` samples, chunk by cycle chunk.
	void render_block(dsp::Buffer<Q<31>> output) {
		UQ<32> phase = phasor_.phase();
		const UQ<32> phase_increment = phasor_.increment();
		Resetter resetter = resetter_;

		WavetableCycleChunker chunker{table_, output, phase_increment.raw(), wave_index_, wave_index_increment_};
		while (std::optional<CycleChunk> chunk = chunker.next()) {
			if (chunk->band != nullptr) {
				SyncWindower windower{chunk->output, resetter, phase, phase_increment, retrigger_phase_};
				while (std::optional<SyncWindower::Window> window = windower.next()) {
					chunk->render(window->output, window->phasor, window->offset);
				}
				phase = windower.phase();
			}
			// The resetter advances past the whole chunk whether or not it had data.
			resetter.phase =
			    dsp::advanced(resetter.phase, resetter.phase_increment, static_cast<uint32_t>(chunk->output.size()));
		}

		phasor_.seek(phase);
	}

private:
	WaveTableData table_;
	Phasor phasor_;
	Resetter resetter_;
	UQ<32> retrigger_phase_;
	int32_t wave_index_;
	int32_t wave_index_increment_;
};
static_assert(block_generator<WavetableSyncOscillator>, "a synced wavetable oscillator fills a block");
static_assert(!std::is_polymorphic_v<WavetableSyncOscillator>,
              "a synced wavetable oscillator fills a block without a vptr");

} // namespace deluge::dsp::fixed
