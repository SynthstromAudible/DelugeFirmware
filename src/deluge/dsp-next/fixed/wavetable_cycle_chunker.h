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

#include "definitions_cxx.hpp" // kInterpolationMaxNumSamples(Magnitude)
#include "dsp-next/core/generator.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/sinc_interpolation.h" // windowed_sinc_apply, SincWindow
#include "dsp-next/fixed/wave_table_data.h"
#include "dsp-next/fixed/windowed_sinc_kernel.h" // windowed_sinc_kernel
#include "util/fixedpoint.h"                     // multiply_32x32_rshift32(_accumulate)_rounded
#include <argon.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::dsp::fixed {

/// Duplicate samples appended to the end of each stored cycle, so the interpolation window's
/// rightmost loads can read past the wrap without a second masked load (legacy
/// WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE).
inline constexpr int32_t kWavetableDuplicateSamplesAtCycleEnd = 7;

namespace wavetable_detail {
// The windowed-sinc table has 2^kSincKernelRowBits sub-sample rows; the phase fraction selects the
// row to read (@ref sinc_kernel_row) and the Q15 blend toward the next row (@ref sinc_row_blend).
constexpr int32_t kSincKernelRowBits = 8; // legacy numBitsInWindowedSyncTableSize
constexpr int32_t kSincRowBlendShift =
    (32 + kInterpolationMaxNumSamplesMagnitude) - 16 - kSincKernelRowBits + 1; // legacy rshiftAmount

/// The @ref kInterpolationMaxNumSamples sample window centred on @p phase within a cycle of
/// 2^magnitude samples, wrapping at the cycle edge.
[[gnu::always_inline]] inline SincWindow load_window(const int16_t* __restrict__ table, uint32_t phase,
                                                     int32_t magnitude) {
	SincWindow window;
	int32_t centre = phase >> (32 - magnitude);
	uint32_t index = centre - (kInterpolationMaxNumSamples >> 1);
	for (std::size_t i = 0; i < kSincWindowVectors; ++i) {
		index &= (1 << magnitude) - 1;
		window[i] = Argon<int16_t>::Load(&table[index]);
		index += Argon<int16_t>::lanes;
	}
	return window;
}

/// The Q15 blend toward the next kernel row (the sub-sample position), for @p phase at this magnitude.
[[gnu::always_inline]] inline int16_t sinc_row_blend(uint32_t phase, int32_t magnitude) {
	return (static_cast<uint32_t>(-phase) >> (kSincRowBlendShift - magnitude)) & 32767;
}

/// The first of the two kernel rows to blend, for @p phase, offset into @p kernel.
[[gnu::always_inline]] inline const int16_t* sinc_kernel_row(const int16_t* kernel, uint32_t phase, int32_t magnitude) {
	// The -5 is for 32 bytes (16 int16 samples) per row of the windowed-sinc table.
	int32_t offset_bytes = static_cast<uint32_t>(-phase)
	                       >> (32 + kInterpolationMaxNumSamplesMagnitude - kSincKernelRowBits - 5 - magnitude);
	offset_bytes &= 0b111100000;
	return reinterpret_cast<const int16_t*>(reinterpret_cast<uintptr_t>(kernel) + offset_bytes);
}
} // namespace wavetable_detail

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

	/// Render @p output_window from @p phasor (per sample: window the band's cycle(s) through the
	/// sinc @ref kernel). @p offset is the window's position within the chunk (0 for the whole chunk),
	/// which positions the cross-cycle blend so a redone sync-crossover sample reuses the same blend.
	/// Caller must have checked @ref band is non-null.
	[[gnu::always_inline]] void render(Buffer output_window, Phasor phasor, int32_t offset) const {
		using namespace wavetable_detail;
		int32_t* __restrict__ sample = output_window.data();
		int32_t const* const end = sample + output_window.size();
		uint32_t phase = phasor.phase();
		uint32_t phase_increment = phasor.increment();
		int32_t magnitude = band->cycleSizeMagnitude;

		if (single_cycle) {
			const int16_t* __restrict__ cycle = band->dataAccessAddress;
			do {
				phase += phase_increment;
				*sample = windowed_sinc_apply(sinc_kernel_row(kernel, phase, magnitude),
				                              sinc_row_blend(phase, magnitude), load_window(cycle, phase, magnitude));
			} while (++sample != end);
			return;
		}

		// Morphing: window each of the two adjacent cycles, then linearly cross-blend by the morph.
		int32_t cycle_stride = band->cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd;
		const int16_t* __restrict__ cycle1 = &band->dataAccessAddress[first_cycle * cycle_stride];
		const int16_t* __restrict__ cycle2 = cycle1 + cycle_stride;
		uint32_t blend =
		    cross_cycle_blend + static_cast<uint32_t>(cross_cycle_blend_increment) * static_cast<uint32_t>(offset);
		do {
			phase += phase_increment;
			// Both cycles share the same sub-sample kernel position, so blend the kernel once.
			SincWindow blended =
			    blend_sinc_kernel(sinc_kernel_row(kernel, phase, magnitude), sinc_row_blend(phase, magnitude));
			int32_t value1 = windowed_sinc_dot(blended, load_window(cycle1, phase, magnitude));
			int32_t value2 = windowed_sinc_dot(blended, load_window(cycle2, phase, magnitude));
			int32_t difference = value2 - value1;
			// value1 is taken a magnitude smaller, because the multiply below shrinks `difference` by one.
			*sample = multiply_accumulate_32x32_rshift32_rounded(value1 >> 1, difference, blend >> 1);
			blend += cross_cycle_blend_increment;
		} while (++sample != end);
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
			return CycleChunk{take(samples_remaining_), 0, band_, 0, 0, kernel_for(band_), /*single_cycle=*/true};
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
		return (band != nullptr) ? windowed_sinc_kernel.select(phase_increment_, band->maxPhaseIncrement) : nullptr;
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
