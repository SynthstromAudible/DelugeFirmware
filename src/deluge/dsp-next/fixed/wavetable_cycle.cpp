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

// The wavetable cycle-render inner loops, relocated verbatim from storage/WaveTable (the hand-
// written NEON is kept as-is for bit-exactness; argon-ifying it is a later, separately-gated pass).

#include "dsp-next/fixed/wavetable_cycle.h"
#include "definitions_cxx.hpp"
#include "util/fixedpoint.h"
#include <arm_neon.h>
#include <cstdint>

namespace deluge::dsp::fixed {

__attribute__((optimize("unroll-loops"))) void
render_wavetable_single_cycle(int32_t* __restrict__ thisSample, int32_t const* bufferEnd, const WaveTableBand& band,
                              uint32_t phase, uint32_t phaseIncrement, const int16_t* __restrict__ kernel) {

	int32_t bandCycleSizeMagnitude = band.cycleSizeMagnitude;
	int16_t const* __restrict__ table = band.dataAccessAddress;

	do {
		phase += phaseIncrement;

		// Work out the location of the waveform data in memory
		int32_t whichValueCentral = (phase >> (32 - bandCycleSizeMagnitude));
		uint32_t whichValue = whichValueCentral - (kInterpolationMaxNumSamples >> 1);
		int32_t whichValueStored[2];

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			whichValue = whichValue & ((1 << bandCycleSizeMagnitude) - 1);
			whichValueStored[i] = whichValue;
			whichValue += 8;
		}

		// Grab the actual waveform data from memory
		int16x8x2_t interpolationBuffer;
		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			interpolationBuffer.val[i] = vld1q_s16(&table[whichValueStored[i]]);
		}

// Get the windowed sinc kernel that we need for this individual audio-sample
#define numBitsInWindowedSyncTableSize 8
#define rshiftAmount ((32 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInWindowedSyncTableSize + 1)

		uint32_t rshifted =
		    ((uint32_t)-phase)
		    >> (rshiftAmount - bandCycleSizeMagnitude); // Warning - rshiftAmount is 13, so bandCycleSizeMagnitude
		                                                // better not be bigger than that!
		int16_t strength2 = rshifted & 32767;

		int32_t windowedSincTableLineOffsetBytes =
		    ((uint32_t)-phase)
		    >> (32 + kInterpolationMaxNumSamplesMagnitude - numBitsInWindowedSyncTableSize - 5
		        - bandCycleSizeMagnitude); // The -5 is for 32 bytes (16 samples) per line in the windowed sinc table.
		windowedSincTableLineOffsetBytes &= 0b111100000;
		int16_t const* __restrict__ sincKernelReadPos =
		    (int16_t const*)((uint32_t)&kernel[0] + windowedSincTableLineOffsetBytes);

		int16x8_t kernelVector[kInterpolationMaxNumSamples >> 3];

		/*
		int16x8x4_t windowedSincReadValues = vld4q_s16(sincKernelReadPos); // Insanely, I could not get this to work.
		Tried reading 2 values, too. I just get some noise from final output.

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
		    int16x8_t difference = vsubq_s16(windowedSincReadValues.val[i + 2], windowedSincReadValues.val[i]);
		    int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
		    kernelVector[i] = vaddq_s16(windowedSincReadValues.val[i], multipliedDifference);
		}
		*/

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			int16x8_t value1 = vld1q_s16(sincKernelReadPos + (i << 3));
			int16x8_t value2 = vld1q_s16(sincKernelReadPos + 16 + (i << 3));

			int16x8_t difference = vsubq_s16(value2, value1);
			int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
			kernelVector[i] = vaddq_s16(value1, multipliedDifference);
		}

		// Apply the windowed sinc kernel to the waveform data
		int32x4_t multiplied;
		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {

			if (i == 0) {
				multiplied = vmull_s16(vget_low_s16(kernelVector[i]), vget_low_s16(interpolationBuffer.val[i]));
			}
			else {
				multiplied =
				    vmlal_s16(multiplied, vget_low_s16(kernelVector[i]), vget_low_s16(interpolationBuffer.val[i]));
			}

			multiplied =
			    vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), vget_high_s16(interpolationBuffer.val[i]));
		}

		int32x2_t twosies = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));
		int32x2_t onesie = vpadd_s32(twosies, twosies);

		int32_t singleCycleFinalValue = vget_lane_s32(onesie, 0);

		*thisSample = singleCycleFinalValue;

	} while (++thisSample != bufferEnd);
}

__attribute__((optimize("unroll-loops"))) void
render_wavetable_cycle(int32_t* __restrict__ thisSample, int32_t const* bufferEnd, int32_t firstCycleNumber,
                       const WaveTableBand& band, uint32_t phase, uint32_t phaseIncrement, uint32_t crossCycleStrength2,
                       int32_t crossCycleStrength2Increment, const int16_t* __restrict__ kernel) {

	int32_t bandCycleSizeMagnitude = band.cycleSizeMagnitude;
	int32_t bandCycleSizeWithDuplicates = band.cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd;
	const int16_t* bandData = band.dataAccessAddress;
	int16_t const* __restrict__ table1 = &bandData[firstCycleNumber * bandCycleSizeWithDuplicates];
	int16_t const* __restrict__ table2 = table1 + bandCycleSizeWithDuplicates;

	do {
		phase += phaseIncrement;

		// Work out the location of the waveform data in memory
		int32_t whichValueCentral = (phase >> (32 - bandCycleSizeMagnitude));
		uint32_t whichValue = whichValueCentral - (kInterpolationMaxNumSamples >> 1);
		int32_t whichValueStored[2];

		for (int32_t i = 0; i < kInterpolationMaxNumSamples >> 3; i++) {
			whichValue = whichValue & ((1 << bandCycleSizeMagnitude) - 1);
			whichValueStored[i] = whichValue;
			whichValue += 8;
		}

		// Grab the actual waveform data from memory, for both cycles that we need for this sample
		int16x8x2_t interpolationBuffer[2];
		for (int32_t i = 0; i < kInterpolationMaxNumSamples >> 3; i++) {
			interpolationBuffer[0].val[i] = vld1q_s16(&table1[whichValueStored[i]]);
		}
		for (int32_t i = 0; i < kInterpolationMaxNumSamples >> 3; i++) {
			interpolationBuffer[1].val[i] = vld1q_s16(&table2[whichValueStored[i]]);
		}

// Get the windowed sinc kernel that we need for this individual audio-sample
#define numBitsInWindowedSyncTableSize 8
#define rshiftAmount ((32 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInWindowedSyncTableSize + 1)

		uint32_t rshifted =
		    ((uint32_t)-phase)
		    >> (rshiftAmount - bandCycleSizeMagnitude); // Warning - rshiftAmount is 13, so bandCycleSizeMagnitude
		                                                // better not be bigger than that!
		int16_t strength2 = rshifted & 32767;

		int32_t windowedSincTableLineOffsetBytes =
		    ((uint32_t)-phase)
		    >> (32 + kInterpolationMaxNumSamplesMagnitude - numBitsInWindowedSyncTableSize - 5
		        - bandCycleSizeMagnitude); // The -5 is for 32 bytes (16 samples) per line in the windowed sinc table.
		windowedSincTableLineOffsetBytes &= 0b111100000;
		int16_t const* __restrict__ sincKernelReadPos =
		    (int16_t const*)((uint32_t)&kernel[0] + windowedSincTableLineOffsetBytes);

		int16x8_t kernelVector[kInterpolationMaxNumSamples >> 3];

		/*
		int16x8x4_t windowedSincReadValues = vld4q_s16(sincKernelReadPos); // Insanely, I could not get this to work.
		Tried reading 2 values, too. I just get some noise from final output.

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
		    int16x8_t difference = vsubq_s16(windowedSincReadValues.val[i + 2], windowedSincReadValues.val[i]);
		    int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
		    kernelVector[i] = vaddq_s16(windowedSincReadValues.val[i], multipliedDifference);
		}
		*/

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			int16x8_t value1 = vld1q_s16(sincKernelReadPos + (i << 3));
			int16x8_t value2 = vld1q_s16(sincKernelReadPos + 16 + (i << 3));

			int16x8_t difference = vsubq_s16(value2, value1);
			int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
			kernelVector[i] = vaddq_s16(value1, multipliedDifference);
		}

		// Apply the windowed sinc kernel to the waveform data
		int32x2_t twosies[2];
		for (int32_t c = 0; c < 2; c++) {
			int32x4_t multiplied;
			for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {

				if (i == 0) {
					multiplied = vmull_s16(vget_low_s16(kernelVector[i]), vget_low_s16(interpolationBuffer[c].val[i]));
				}
				else {
					multiplied = vmlal_s16(multiplied, vget_low_s16(kernelVector[i]),
					                       vget_low_s16(interpolationBuffer[c].val[i]));
				}

				multiplied =
				    vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), vget_high_s16(interpolationBuffer[c].val[i]));
			}

			twosies[c] = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));
		}

		// We now have one value for each cycle, so linearly interpolate between those.
		int32x2_t onesie = vpadd_s32(twosies[0], twosies[1]);
		int32_t value1 = vget_lane_s32(onesie, 0);
		int32_t difference = vget_lane_s32(onesie, 1) - value1;

		int32_t waveTableFinalValue = multiply_accumulate_32x32_rshift32_rounded(
		    value1 >> 1, difference,
		    crossCycleStrength2 >> 1); // Have to make value1 a magnitude smaller, because the difference is getting a
		                               // magnitude smaller as a multiplication like this always does.

		*thisSample = waveTableFinalValue;

		crossCycleStrength2 += crossCycleStrength2Increment;

	} while (++thisSample != bufferEnd);
}

// These were #define'd inside the relocated loop bodies (file scope); drop them so they don't leak
// into the next translation unit of the dsp-next unity build.
#undef numBitsInWindowedSyncTableSize
#undef rshiftAmount

} // namespace deluge::dsp::fixed
