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
#include "util/misc.h"
#include <argon.hpp>

// Renders 4 wave values (a "vector") together in one go.
[[gnu::always_inline]] static inline Argon<int32_t> //<
waveRenderingFunctionGeneral(uint32_t& phaseTemp, int32_t phaseIncrement, uint32_t _phaseToAdd, const int16_t* table,
                             int32_t tableSizeMagnitude) {
	Argon<uint32_t> readValue = 0U;
	ArgonHalf<uint16_t> strength2 = 0U;

	util::constexpr_for<0, 4, 1>([&]<int i>() {
		phaseTemp += phaseIncrement;
		uint32_t rshifted = phaseTemp >> (32 - 16 - tableSizeMagnitude);
		strength2[i] = rshifted;

		uint32_t whichValue = phaseTemp >> (32 - tableSizeMagnitude);
		uint32_t* readAddress = (uint32_t*)((uint32_t)table + (whichValue << 1));
		readValue[i].Load(readAddress);
	});

	strength2 = vshr_n_u16(strength2, 1);
	ArgonHalf<int16_t> value1 = vreinterpret_s16_u16(vmovn_u32(readValue));
	ArgonHalf<int16_t> value2 = vreinterpret_s16_u16(vshrn_n_u32(readValue, 16));
	Argon<int32_t> value1Big = vshll_n_s16(value1, 16);

	ArgonHalf<int16_t> difference = vsub_s16(value2, value1);
	return vqdmlal_s16(value1Big, difference, vreinterpret_s16_u16(strength2));
}

// Renders 4 wave values (a "vector") together in one go - special case for pulse waves with variable width.
[[gnu::always_inline]] static inline Argon<int32_t> //<
waveRenderingFunctionPulse(uint32_t& phaseTemp, int32_t phaseIncrement, uint32_t phaseToAdd, const int16_t* table,
                           int32_t tableSizeMagnitude) {
	ArgonHalf<int16_t> rshiftedA = 0;
	ArgonHalf<int16_t> rshiftedB = 0;
	Argon<uint32_t> readValueA = 0U;
	Argon<uint32_t> readValueB = 0U;

	int32_t rshiftAmount = (32 - tableSizeMagnitude - 16);

	util::constexpr_for<0, 4, 1>([&]<int i>() {
		{
			phaseTemp += phaseIncrement;
			rshiftedA[i] = phaseTemp >> rshiftAmount;

			uint32_t whichValue = phaseTemp >> (32 - tableSizeMagnitude);
			uint32_t* readAddress = (uint32_t*)((uint32_t)table + (whichValue << 1));
			readValueA[i].Load(readAddress);
		}

		{
			uint32_t phaseLater = phaseTemp + phaseToAdd;
			rshiftedB[i] = phaseLater >> rshiftAmount;

			uint32_t whichValue = phaseLater >> (32 - tableSizeMagnitude);
			uint32_t* readAddress = (uint32_t*)((uint32_t)table + (whichValue << 1));
			readValueB[i].Load(readAddress);
		}
	});

	ArgonHalf<int16_t> valueA1 = vreinterpret_s16_u16(vmovn_u32(readValueA));
	ArgonHalf<int16_t> valueA2 = vreinterpret_s16_u16(vshrn_n_u32(readValueA, 16));

	ArgonHalf<int16_t> valueB1 = vreinterpret_s16_u16(vmovn_u32(readValueB));
	ArgonHalf<int16_t> valueB2 = vreinterpret_s16_u16(vshrn_n_u32(readValueB, 16));

	/* Sneakily do this backwards to flip the polarity of the output, which we need to do anyway */
	ArgonHalf<int16_t> const32768 = vdup_n_s16(-32768);
	ArgonHalf<int16_t> const32767 = vdup_n_s16(32767);
	ArgonHalf<int16_t> strengthA1 = vorr_s16(rshiftedA, const32768);
	ArgonHalf<int16_t> strengthA2 = vsub_s16(const32768, strengthA1);

	Argon<int32_t> multipliedValueA2 = vqdmull_s16(strengthA2, valueA2);
	Argon<int32_t> outputA = vqdmlal_s16(multipliedValueA2, strengthA1, valueA1);

	ArgonHalf<int16_t> strengthB2 = vand_s16(rshiftedB, const32767);
	ArgonHalf<int16_t> strengthB1 = vsub_s16(const32767, strengthB2);

	Argon<int32_t> multipliedValueB2 = vqdmull_s16(strengthB2, valueB2);
	Argon<int32_t> outputB = vqdmlal_s16(multipliedValueB2, strengthB1, valueB1);

	Argon<int32_t> output = vqrdmulhq_s32(outputA, outputB);
	return vshlq_n_s32(output, 1);
}
