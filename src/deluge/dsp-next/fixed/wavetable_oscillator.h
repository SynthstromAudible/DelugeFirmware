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
#include "dsp-next/fixed/wave_table_data.h"
#include "dsp-next/fixed/wavetable_cycle_chunker.h"
#include <cstdint>
#include <optional>

namespace deluge::dsp::fixed {

/// @brief A free-running (non-synced) wavetable oscillator, as a dsp-next component.
///
/// Walks the @ref WaveTableData view as @ref CycleChunk "chunks" (single-cycle: one; morphing: one
/// per cross-cycle run) and renders each straight from the running phase. Bit-identical to the
/// legacy non-sync `WaveTable::render`.
class [[gnu::hot]] WavetableOscillator : public BlockGenerator<q31> {
public:
	WavetableOscillator(WaveTableData table, uint32_t phase, uint32_t increment, int32_t wave_index,
	                    int32_t wave_index_increment)
	    : table_{table}, phasor_{phase, increment}, wave_index_{wave_index},
	      wave_index_increment_{wave_index_increment} {}

	/// The phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		uint32_t phase = phasor_.phase();
		uint32_t phase_increment = phasor_.increment();

		WavetableCycleChunker chunker{table_, output, phase_increment, wave_index_, wave_index_increment_};
		while (std::optional<CycleChunk> chunk = chunker.next()) {
			if (chunk->band == nullptr) {
				continue; // no band has data for this cycle pair — skip it (phase doesn't advance)
			}
			chunk->render(chunk->output, Phasor{phase, phase_increment}, 0);
			phase += phase_increment * static_cast<uint32_t>(chunk->output.size());
		}

		phasor_.seek(phase);
	}

private:
	WaveTableData table_;
	Phasor phasor_;
	int32_t wave_index_;
	int32_t wave_index_increment_;
};

} // namespace deluge::dsp::fixed
