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
#include <limits>
#include <utility>

// Renders 4 wave values (a "vector") together in one go.
[[gnu::always_inline]] static inline std::pair<Argon<int32_t>, uint32_t> //<
waveRenderingFunctionGeneral(uint32_t phase, int32_t phaseIncrement, uint32_t _phaseToAdd, const int16_t* table,
                             int32_t tableSizeMagnitude) {
	Argon<uint32_t> phaseVector = Argon<uint32_t>{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, phaseIncrement);
	Argon<uint32_t> indices = phaseVector >> (32 - tableSizeMagnitude);
	ArgonHalf<uint16_t> strength2 = indices.ShiftRightNarrow<16>() >> 1;
	auto [value1, value2] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(table, indices);

	// this is a standard linear interpolation of a + (b - a) * fractional
	auto output = value1.ShiftLeftLong<16>().MultiplyDoubleAddSaturateLong(value2 - value1, strength2.As<int16_t>());
	return {output, phase + phaseIncrement * 4};
}

// Renders 4 wave values (a "vector") together in one go - special case for pulse waves with variable width.
[[gnu::always_inline]] static inline std::pair<Argon<int32_t>, uint32_t> //<
waveRenderingFunctionPulse(uint32_t phase, int32_t phaseIncrement, uint32_t phaseToAdd, const int16_t* table,
                           int32_t tableSizeMagnitude) {

	const int32_t rshiftAmount = (32 - tableSizeMagnitude - 16);

	Argon<uint32_t> phaseVector = Argon<uint32_t>{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, phaseIncrement);
	Argon<uint32_t> phaseLater = phaseVector + phaseToAdd;

	Argon<uint32_t> indicesA = phaseVector >> (32 - tableSizeMagnitude);
	ArgonHalf<int16_t> rshiftedA =
	    indicesA.ShiftRightNarrow<16>().BitwiseAnd(std::numeric_limits<int16_t>::max()).As<int16_t>();
	auto [valueA1, valueA2] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(table, indicesA);

	Argon<uint32_t> indicesB = phaseLater >> (32 - tableSizeMagnitude);
	ArgonHalf<int16_t> rshiftedB =
	    indicesB.ShiftRightNarrow<16>().BitwiseAnd(std::numeric_limits<int16_t>::max()).As<int16_t>();
	auto [valueB1, valueB2] = ArgonHalf<int16_t>::LoadGatherInterleaved<2>(table, indicesB);

	/* Sneakily do this backwards to flip the polarity of the output, which we need to do anyway */
	ArgonHalf<int16_t> strengthA1 = rshiftedA | std::numeric_limits<int16_t>::min();
	ArgonHalf<int16_t> strengthA2 = std::numeric_limits<int16_t>::min() - strengthA1;

	Argon<int32_t> outputA = strengthA2                               //<
	                             .MultiplyDoubleSaturateLong(valueA2) //<
	                             .MultiplyDoubleAddSaturateLong(strengthA1, valueA1);

	ArgonHalf<int16_t> strengthB2 = rshiftedB & std::numeric_limits<int16_t>::max();
	ArgonHalf<int16_t> strengthB1 = std::numeric_limits<int16_t>::max() - strengthB2;

	Argon<int32_t> outputB = strengthB2                               //<
	                             .MultiplyDoubleSaturateLong(valueB2) //<
	                             .MultiplyDoubleAddSaturateLong(strengthB1, valueB1);

	auto output = outputA.MultiplyRoundFixedPoint(outputB) << 1; // (a *. b) << 1 (average?)
	return {output, phase + phaseIncrement * 4};
}
