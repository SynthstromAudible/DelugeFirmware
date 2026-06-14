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

#include "dsp-next/core/generator.h" // Buffer, q31
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/sinc_kernel.h"
#include "dsp-next/fixed/wave_table_data.h"
#include "dsp-next/fixed/wavetable_cycle.h"
#include "util/fixedpoint.h" // multiply_32x32_rshift32_rounded
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::dsp::fixed {

/// @brief One renderable chunk of a wavetable — knows how to render itself.
///
/// For a morphing wavetable it is a run of @ref output over which the wave index stays within one
/// cycle pair (@ref first_cycle, +1), cross-blended by @ref cross_cycle_blend; for a single-cycle
/// wavetable it is a whole block over the one cycle. @ref render fills a (sub-)buffer from a phasor
/// — the oscillators call it for the whole chunk (free-running) or per sync window. @ref band is
/// null when no band holds data for this cycle pair (the chunk is then skipped).
struct CycleChunk {
	Buffer output;
	int32_t first_cycle;
	const WaveTableBand* band;
	uint32_t cross_cycle_blend;          ///< the cross-cycle blend (morph) at the chunk's first sample.
	int32_t cross_cycle_blend_increment; ///< how the blend advances per sample.
	const int16_t* kernel;               ///< the windowed-sinc kernel for this chunk's band + pitch.
	bool single_cycle;

	/// Render @p output_window from @p phasor. @p offset is the window's position within the chunk
	/// (0 for the whole chunk), which positions the cross-cycle blend so a redone sync-crossover
	/// sample reuses the same blend. Caller must have checked @ref band is non-null.
	[[gnu::always_inline]] void render(Buffer output_window, Phasor phasor, int32_t offset) const {
		if (single_cycle) {
			render_wavetable_single_cycle(output_window, *band, phasor, kernel);
		}
		else {
			uint32_t blend =
			    cross_cycle_blend + static_cast<uint32_t>(cross_cycle_blend_increment) * static_cast<uint32_t>(offset);
			render_wavetable_cycle(output_window, first_cycle, *band, phasor, blend, cross_cycle_blend_increment,
			                       kernel);
		}
	}
};

/// @brief Iterates a wavetable block as renderable @ref CycleChunk "chunks".
///
/// A single-cycle wavetable yields one chunk (the whole block); a morphing one is split wherever the
/// wave index crosses a cycle boundary, each chunk carrying its cycle pair, per-sample blend, and
/// the band (and kernel) that has data for it. This is the shared machinery of
/// @ref WavetableOscillator and @ref WavetableSyncOscillator — they only differ in whether they
/// render each chunk straight or through a @ref SyncWindower:
/// @code
///   WavetableCycleChunker chunker{table, output, phaseIncrement, waveIndex, waveIndexIncrement};
///   while (std::optional<CycleChunk> chunk = chunker.next()) { ... chunk->render(...) ... }
/// @endcode
/// Bit-exact with the legacy `WaveTable::render` loop.
class WavetableCycleChunker {
public:
	WavetableCycleChunker(const WaveTableData& table, Buffer output, uint32_t phase_increment, int32_t wave_index,
	                      int32_t wave_index_increment)
	    : table_{&table}, phase_increment_{phase_increment}, next_output_{output.data()},
	      samples_remaining_{static_cast<int32_t>(output.size())},
	      wave_index_scaled_{multiply_32x32_rshift32_rounded(table.wave_index_multiplier(), wave_index)},
	      wave_index_increment_scaled_{
	          multiply_32x32_rshift32_rounded(table.wave_index_multiplier(), wave_index_increment)},
	      blend_shift_{32 + table.num_cycle_transitions_magnitude() - kWaveIndexScaledInputBits},
	      blend_increment_{wave_index_increment_scaled_ << blend_shift_},
	      band_index_{table.select_band(phase_increment)}, band_{&table.band(band_index_)} {}

	/// The next chunk, or `nullopt` once the block is done.
	[[gnu::hot]] std::optional<CycleChunk> next() {
		if (samples_remaining_ <= 0) {
			return std::nullopt;
		}

		// A single-cycle wavetable is one chunk over the band picked for this pitch.
		if (table_->num_cycles() <= 1) {
			CycleChunk chunk{take(samples_remaining_), 0, band_, 0, 0, kernel_for(band_), /*single_cycle=*/true};
			return chunk;
		}

		int32_t cycle_shift = kWaveIndexScaledInputBits - table_->num_cycle_transitions_magnitude();
		int32_t first_cycle = wave_index_scaled_ >> cycle_shift;
		int32_t last_sample_cycle =
		    (wave_index_scaled_ + wave_index_increment_scaled_ * (samples_remaining_ - 1)) >> cycle_shift;

		// Does this run span into the next cycle as the wave index increments? If so, only go up to
		// the boundary now.
		int32_t chunk_length = samples_remaining_;
		if (first_cycle != last_sample_cycle) {
			int32_t cycle_size_in_wave_index = 1 << cycle_shift;
			int32_t wave_index_within_cycle = wave_index_scaled_ & (cycle_size_in_wave_index - 1);
			int32_t distance_to_cycle_end = (wave_index_increment_scaled_ >= 0)
			                                    ? (cycle_size_in_wave_index - 1 - wave_index_within_cycle)
			                                    : (wave_index_within_cycle);
			int32_t wave_index_increment_abs = wave_index_increment_scaled_;
			if (wave_index_increment_abs < 0) {
				wave_index_increment_abs = -wave_index_increment_abs;
			}
			int32_t increments_until_boundary =
			    static_cast<uint32_t>(distance_to_cycle_end) / static_cast<uint32_t>(wave_index_increment_abs);
			// +1: we only increment *between* the samples we render, so 1 increment = 2 samples.
			chunk_length = increments_until_boundary + 1;
		}

		uint32_t cross_cycle_blend = wave_index_scaled_ << blend_shift_;

		// Walk up to a band that has data for both cycle indexes we'll interpolate between.
		while (band_->fromCycleNumber > first_cycle || band_->toCycleNumber <= first_cycle + 1) {
			band_index_++;
			if (band_index_ == table_->num_bands()) {
				break;
			}
			band_ = &table_->band(band_index_);
		}
		const WaveTableBand* band = (band_index_ < table_->num_bands()) ? band_ : nullptr;

		wave_index_scaled_ += wave_index_increment_scaled_ * chunk_length;
		return CycleChunk{take(chunk_length), first_cycle,           band, cross_cycle_blend, blend_increment_,
		                  kernel_for(band),   /*single_cycle=*/false};
	}

private:
	static constexpr int32_t kWaveIndexScaledInputBits = 30; // legacy NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT

	/// Carve @p length samples off the front of the remaining block.
	Buffer take(int32_t length) {
		Buffer slice{next_output_, static_cast<std::size_t>(length)};
		next_output_ += length;
		samples_remaining_ -= length;
		return slice;
	}

	const int16_t* kernel_for(const WaveTableBand* band) const {
		return (band != nullptr) ? select_sinc_kernel(phase_increment_, band->maxPhaseIncrement) : nullptr;
	}

	const WaveTableData* table_;
	uint32_t phase_increment_;
	q31* next_output_;
	int32_t samples_remaining_;
	int32_t wave_index_scaled_;
	int32_t wave_index_increment_scaled_;
	int32_t blend_shift_;
	int32_t blend_increment_;
	int32_t band_index_;
	const WaveTableBand* band_;
};

} // namespace deluge::dsp::fixed
