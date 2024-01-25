/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#define numBitsInTableSize 8
#define rshiftAmount                                                                                                   \
	((24 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInTableSize                                             \
	 + 1) // that's (numBitsInInput - 16 - numBitsInTableSize); = 4 for now

#if rshiftAmount >= 0
uint32_t rshifted = oscPos >> rshiftAmount;
#else
uint32_t rshifted = oscPos << (-rshiftAmount);
#endif

int16_t strength2 = rshifted & 32767;

int32_t progressSmall = oscPos >> (24 + kInterpolationMaxNumSamplesMagnitude - numBitsInTableSize);

int16x8_t kernelVector[kInterpolationMaxNumSamples >> 3];

for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
	int16x8_t value1 = vld1q_s16(&windowedSincKernel[whichKernel][progressSmall][i << 3]);
	int16x8_t value2 = vld1q_s16(&windowedSincKernel[whichKernel][progressSmall + 1][i << 3]);
	int16x8_t difference = vsubq_s16(value2, value1);
	int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
	kernelVector[i] = vaddq_s16(value1, multipliedDifference);
}

int32x4_t multiplied;

for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {

	if (i == 0)
		multiplied = vmull_s16(vget_low_s16(kernelVector[i]), interpolationBuffer[0][i << 1]);
	else
		multiplied = vmlal_s16(multiplied, vget_low_s16(kernelVector[i]), interpolationBuffer[0][i << 1]);

	multiplied = vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), interpolationBuffer[0][(i << 1) + 1]);
}

int32x2_t twosies = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));

sampleRead[0] = vget_lane_s32(twosies, 0) + vget_lane_s32(twosies, 1);

if (numChannelsNow == 2) {

	int32x4_t multiplied;

	for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {

		if (i == 0)
			multiplied = vmull_s16(vget_low_s16(kernelVector[i]), interpolationBuffer[1][i << 1]);
		else
			multiplied = vmlal_s16(multiplied, vget_low_s16(kernelVector[i]), interpolationBuffer[1][i << 1]);

		multiplied = vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), interpolationBuffer[1][(i << 1) + 1]);
	}

	int32x2_t twosies = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));

	sampleRead[1] = vget_lane_s32(twosies, 0) + vget_lane_s32(twosies, 1);
}
