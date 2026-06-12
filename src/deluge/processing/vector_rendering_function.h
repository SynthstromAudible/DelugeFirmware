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
#pragma once
#include "util/misc.h"
#include <argon.hpp>
#include <limits>
#include <utility>

// Renders 4 wave values (a "vector") together in one go.
[[gnu::always_inline]] static inline std::pair<Argon<int32_t>, uint32_t> //<
waveRenderingFunctionGeneral(uint32_t phase, int32_t phaseIncrement, uint32_t _phaseToAdd, const int16_t* table,
                             int32_t tableSizeMagnitude) {
	Argon<uint32_t> readValue = 0U;
	ArgonHalf<uint16_t> strength2 = 0U;

	util::constexpr_for<0, 4, 1>([&]<int i>() {
		phase += phaseIncrement;
		uint32_t rshifted = phase >> (32 - 16 - tableSizeMagnitude);
		strength2[i] = rshifted;

		uint32_t whichValue = phase >> (32 - tableSizeMagnitude);
		uint32_t* readAddress = (uint32_t*)((uintptr_t)table + (whichValue << 1));
		readValue[i].Load(readAddress);
	});

	strength2 = strength2 >> 1;
	ArgonHalf<int16_t> value1 = readValue.Narrow().As<int16_t>();
	ArgonHalf<int16_t> value2 = readValue.ShiftRightNarrow<16>().As<int16_t>();
	Argon<int32_t> value1Big = value1.ShiftLeftLong<16>();

	ArgonHalf<int16_t> difference = value2 - value1;
	auto output = value1Big.MultiplyDoubleAddSaturateLong(difference, strength2.As<int16_t>());
	return {output, phase};
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

	Argon<uint32_t> indicesB = phaseLater >> (32 - tableSizeMagnitude);
	ArgonHalf<int16_t> rshiftedB =
	    indicesB.ShiftRightNarrow<16>().BitwiseAnd(std::numeric_limits<int16_t>::max()).As<int16_t>();

	// Load the {table[i], table[i+1]} interpolation pairs as single u32 lane loads,
	// the same pattern as waveRenderingFunctionGeneral above.
	Argon<uint32_t> readValueA{0U};
	Argon<uint32_t> readValueB{0U};
	util::constexpr_for<0, 4, 1>([&]<int i>() {
		uint32_t phaseHere = phase + phaseIncrement * (i + 1);
		uint32_t whichValueA = phaseHere >> (32 - tableSizeMagnitude);
		readValueA[i].Load((uint32_t*)((uintptr_t)table + (whichValueA << 1)));
		uint32_t whichValueB = (phaseHere + phaseToAdd) >> (32 - tableSizeMagnitude);
		readValueB[i].Load((uint32_t*)((uintptr_t)table + (whichValueB << 1)));
	});
	ArgonHalf<int16_t> valueA1 = readValueA.Narrow().As<int16_t>();
	ArgonHalf<int16_t> valueA2 = readValueA.ShiftRightNarrow<16>().As<int16_t>();
	ArgonHalf<int16_t> valueB1 = readValueB.Narrow().As<int16_t>();
	ArgonHalf<int16_t> valueB2 = readValueB.ShiftRightNarrow<16>().As<int16_t>();

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

	auto output = outputA.MultiplyRoundFixedQMax(outputB) << 1; // (a *. b) << 1 (average?)
	return {output, phase + phaseIncrement * 4};
}
