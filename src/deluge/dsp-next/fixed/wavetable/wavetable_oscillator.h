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
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "dsp-next/fixed/wavetable/wavetable_cycle_chunker.h"
#include <cstdint>
#include <optional>
#include <type_traits>

namespace deluge::dsp::fixed {

/// @brief A free-running (non-synced) wavetable oscillator, as a dsp-next component.
///
/// Walks the @ref WaveTableData view as @ref CycleChunk "chunks" (single-cycle: one; morphing: one
/// per cross-cycle run) and renders each straight from the running phase. A plain @ref block_generator with no vptr:
/// chunks are block-level, so there is no per-sample `render()`.
class WavetableOscillator {
public:
	using value_type = q31; ///< q31 wave samples.

	WavetableOscillator(WaveTableData table, uint32_t phase, uint32_t increment, int32_t wave_index,
	                    int32_t wave_index_increment)
	    : table_{table}, phasor_{phase, increment}, wave_index_{wave_index},
	      wave_index_increment_{wave_index_increment} {}

	/// The phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	/// @brief Fill @p output with the next `output.size()` samples, chunk by cycle chunk.
	void render_block(Buffer output) {
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
static_assert(block_generator<WavetableOscillator>, "a wavetable oscillator fills a block");
static_assert(!std::is_polymorphic_v<WavetableOscillator>, "a wavetable oscillator fills a block without a vptr");

} // namespace deluge::dsp::fixed
