/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>

// Hard-coded "for-loop" for the below function.
template <int i>
inline uint32x4_t waveRenderingFunctionGeneralForLoop(const uint32x4_t readValue, uint16x4_t& strength2,
                                                      uint32_t& phaseTemp, int32_t phaseIncrement, const int16_t* table,
                                                      int32_t tableSizeMagnitude) {
	phaseTemp += phaseIncrement;
	uint32_t rshifted = phaseTemp >> (32 - 16 - tableSizeMagnitude);
	strength2 = vset_lane_u16(rshifted, strength2, i);

	uint32_t whichValue = phaseTemp >> (32 - tableSizeMagnitude);
	auto* readAddress = reinterpret_cast<uint32_t*>((uint32_t)table + (whichValue << 1));

	return vld1q_lane_u32(readAddress, readValue, i);
}

// Renders 4 wave values (a "vector") together in one go.
inline int32x4_t waveRenderingFunctionGeneral(uint32_t& phaseTemp, int32_t phaseIncrement, uint32_t _phaseToAdd, const int16_t* table,
                                              int32_t tableSizeMagnitude) {
	uint32x4_t readValue;
	uint16x4_t strength2;

	/* Need to use a macro rather than a for loop here, otherwise won't compile with less than O2. */
	readValue = waveRenderingFunctionGeneralForLoop<0>(readValue, strength2, phaseTemp, phaseIncrement, table,
	                                                   tableSizeMagnitude);
	readValue = waveRenderingFunctionGeneralForLoop<1>(readValue, strength2, phaseTemp, phaseIncrement, table,
	                                                   tableSizeMagnitude);
	readValue = waveRenderingFunctionGeneralForLoop<2>(readValue, strength2, phaseTemp, phaseIncrement, table,
	                                                   tableSizeMagnitude);
	readValue = waveRenderingFunctionGeneralForLoop<3>(readValue, strength2, phaseTemp, phaseIncrement, table,
	                                                   tableSizeMagnitude);

	strength2 = vshr_n_u16(strength2, 1);
	int16x4_t value1 = vreinterpret_s16_u16(vmovn_u32(readValue));
	int16x4_t value2 = vreinterpret_s16_u16(vshrn_n_u32(readValue, 16));
	int32x4_t value1Big = vshll_n_s16(value1, 16);

	int16x4_t difference = vsub_s16(value2, value1);

	// valueVector
	return vqdmlal_s16(value1Big, difference, vreinterpret_s16_u16(strength2));
}

struct SimdShiftRead {
	int16x4_t rshifted;
	uint32x4_t readValue;
};

template <int i>
inline void waveRenderingFunctionPulseForLoopFragment(SimdShiftRead& shift_read, const uint32_t phase,
                                                               int32_t rshiftAmount, const int16_t* table,
                                                               int32_t tableSizeMagnitude) {
	shift_read.rshifted = vset_lane_s16(phase >> rshiftAmount, shift_read.rshifted, i);

	uint32_t whichValue = phase >> (32 - tableSizeMagnitude);
	auto* readAddress = reinterpret_cast<uint32_t*>((uint32_t)table + (whichValue << 1));
	shift_read.readValue = vld1q_lane_u32(readAddress, shift_read.readValue, i);
}

// Hard-coded "for-loop" for the below function.
template <int i>
inline void waveRenderingFunctionPulseForLoop(SimdShiftRead& a, SimdShiftRead& b, uint32_t& phaseTemp,
                                              int32_t phaseIncrement, uint32_t phaseToAdd, int32_t rshiftAmount,
                                              const int16_t* table, int32_t tableSizeMagnitude) {
	// A
	phaseTemp += phaseIncrement;
	waveRenderingFunctionPulseForLoopFragment<i>(a, phaseTemp, rshiftAmount, table, tableSizeMagnitude);

	// B
	uint32_t phaseLater = phaseTemp + phaseToAdd;
	waveRenderingFunctionPulseForLoopFragment<i>(b, phaseLater, rshiftAmount, table, tableSizeMagnitude);
}

// Renders 4 wave values (a "vector") together in one go - special case for pulse waves with variable width.
inline int32x4_t waveRenderingFunctionPulse(uint32_t& phaseTemp, int32_t phaseIncrement, uint32_t phaseToAdd,
                                            const int16_t* table, int32_t tableSizeMagnitude) {

	SimdShiftRead a{};
	SimdShiftRead b{};

	int32_t rshiftAmount = (32 - tableSizeMagnitude - 16);

	/* Need to unroll for loop here, otherwise won't compile with less than O2. */
	waveRenderingFunctionPulseForLoop<0>(a, b, phaseTemp, phaseIncrement, phaseToAdd, rshiftAmount, table,
	                                     tableSizeMagnitude);
	waveRenderingFunctionPulseForLoop<1>(a, b, phaseTemp, phaseIncrement, phaseToAdd, rshiftAmount, table,
	                                     tableSizeMagnitude);
	waveRenderingFunctionPulseForLoop<2>(a, b, phaseTemp, phaseIncrement, phaseToAdd, rshiftAmount, table,
	                                     tableSizeMagnitude);
	waveRenderingFunctionPulseForLoop<3>(a, b, phaseTemp, phaseIncrement, phaseToAdd, rshiftAmount, table,
	                                     tableSizeMagnitude);

	int16x4_t valueA1 = vreinterpret_s16_u16(vmovn_u32(a.readValue));
	int16x4_t valueA2 = vreinterpret_s16_u16(vshrn_n_u32(a.readValue, 16));

	int16x4_t valueB1 = vreinterpret_s16_u16(vmovn_u32(b.readValue));
	int16x4_t valueB2 = vreinterpret_s16_u16(vshrn_n_u32(b.readValue, 16));

	/* Sneakily do this backwards to flip the polarity of the output, which we need to do anyway */
	int16x4_t const32768 = vdup_n_s16(-32768);
	int16x4_t const32767 = vdup_n_s16(32767);

	int16x4_t strengthA1 = vorr_s16(a.rshifted, const32768);
	int16x4_t strengthA2 = vsub_s16(const32768, strengthA1);

	int32x4_t multipliedValueA2 = vqdmull_s16(strengthA2, valueA2);
	int32x4_t outputA = vqdmlal_s16(multipliedValueA2, strengthA1, valueA1);

	int16x4_t strengthB2 = vand_s16(b.rshifted, const32767);
	int16x4_t strengthB1 = vsub_s16(const32767, strengthB2);

	int32x4_t multipliedValueB2 = vqdmull_s16(strengthB2, valueB2);
	int32x4_t outputB = vqdmlal_s16(multipliedValueB2, strengthB1, valueB1);

	int32x4_t output = vqrdmulhq_s32(outputA, outputB);

	// valueVector
	return vshlq_n_s32(output, 1);
}
