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

#include "dsp-next/fixed/wavetable_cycle.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/sinc_interpolation.h"
#include "util/fixedpoint.h" // multiply_accumulate_32x32_rshift32_rounded
#include <argon.hpp>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {
namespace {

// The windowed-sinc table has 2^kSincKernelRowBits sub-sample rows; the phase fraction selects the
// row to read (@ref sinc_kernel_row) and the Q15 blend toward the next row (@ref sinc_row_blend).
constexpr int32_t kSincKernelRowBits = 8; // legacy numBitsInWindowedSyncTableSize
constexpr int32_t kSincRowBlendShift =
    (32 + kInterpolationMaxNumSamplesMagnitude) - 16 - kSincKernelRowBits + 1; // legacy rshiftAmount

/// The @ref kInterpolationMaxNumSamples sample window centred on @p phase within a cycle of
/// 2^magnitude samples, wrapping at the cycle edge. (The cycle's trailing duplicate samples let the
/// rightmost loads read past the wrap without a second masked load.)
[[gnu::always_inline]] SincWindow load_window(const int16_t* __restrict__ table, uint32_t phase, int32_t magnitude) {
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

/// The Q15 blend toward the next kernel row (the sub-sample position), for @p phase at this cycle
/// magnitude.
[[gnu::always_inline]] int16_t sinc_row_blend(uint32_t phase, int32_t magnitude) {
	return (static_cast<uint32_t>(-phase) >> (kSincRowBlendShift - magnitude)) & 32767;
}

/// The first of the two kernel rows to blend, for @p phase, offset into @p kernel.
[[gnu::always_inline]] const int16_t* sinc_kernel_row(const int16_t* kernel, uint32_t phase, int32_t magnitude) {
	// The -5 is for 32 bytes (16 int16 samples) per row of the windowed-sinc table.
	int32_t offset_bytes = static_cast<uint32_t>(-phase)
	                       >> (32 + kInterpolationMaxNumSamplesMagnitude - kSincKernelRowBits - 5 - magnitude);
	offset_bytes &= 0b111100000;
	return reinterpret_cast<const int16_t*>(reinterpret_cast<uintptr_t>(kernel) + offset_bytes);
}

} // namespace

void render_wavetable_single_cycle(Buffer output, const WaveTableBand& band, Phasor phasor,
                                   const int16_t* __restrict__ kernel) {
	int32_t* __restrict__ thisSample = output.data();
	int32_t const* const bufferEnd = output.data() + output.size();
	uint32_t phase = phasor.phase();
	uint32_t phaseIncrement = phasor.increment();
	int32_t magnitude = band.cycleSizeMagnitude;
	int16_t const* __restrict__ table = band.dataAccessAddress;

	do {
		phase += phaseIncrement;
		*thisSample = windowed_sinc_apply(sinc_kernel_row(kernel, phase, magnitude), sinc_row_blend(phase, magnitude),
		                                  load_window(table, phase, magnitude));
	} while (++thisSample != bufferEnd);
}

void render_wavetable_cycle(Buffer output, int32_t firstCycleNumber, const WaveTableBand& band, Phasor phasor,
                            uint32_t crossCycleBlend, int32_t crossCycleBlendIncrement,
                            const int16_t* __restrict__ kernel) {
	int32_t* __restrict__ thisSample = output.data();
	int32_t const* const bufferEnd = output.data() + output.size();
	uint32_t phase = phasor.phase();
	uint32_t phaseIncrement = phasor.increment();
	int32_t magnitude = band.cycleSizeMagnitude;
	int32_t cycleSizeWithDuplicates = band.cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd;
	const int16_t* bandData = band.dataAccessAddress;
	int16_t const* __restrict__ table1 = &bandData[firstCycleNumber * cycleSizeWithDuplicates];
	int16_t const* __restrict__ table2 = table1 + cycleSizeWithDuplicates;

	do {
		phase += phaseIncrement;
		const int16_t* kernel_row = sinc_kernel_row(kernel, phase, magnitude);
		int16_t row_blend = sinc_row_blend(phase, magnitude);

		// Windowed-sinc each of the two adjacent cycles, then linearly cross-blend by the morph.
		int32_t value1 = windowed_sinc_apply(kernel_row, row_blend, load_window(table1, phase, magnitude));
		int32_t value2 = windowed_sinc_apply(kernel_row, row_blend, load_window(table2, phase, magnitude));
		int32_t difference = value2 - value1;
		// value1 is taken a magnitude smaller, because the multiply below shrinks `difference` by one.
		*thisSample = multiply_accumulate_32x32_rshift32_rounded(value1 >> 1, difference, crossCycleBlend >> 1);

		crossCycleBlend += crossCycleBlendIncrement;
	} while (++thisSample != bufferEnd);
}

} // namespace deluge::dsp::fixed
