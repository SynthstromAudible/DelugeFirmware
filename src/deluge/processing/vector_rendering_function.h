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
	Argon<uint32_t> readValue = 0U;
	ArgonHalf<uint16_t> strength2 = 0U;

	util::constexpr_for<0, 4, 1>([&]<int i>() {
		phase += phaseIncrement;
		uint32_t rshifted = phase >> (32 - 16 - tableSizeMagnitude);
		strength2[i] = rshifted;

		uint32_t whichValue = phase >> (32 - tableSizeMagnitude);
		uint32_t* readAddress = (uint32_t*)((uint32_t)table + (whichValue << 1));
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
	ArgonHalf<int16_t> rshiftedA = 0;
	ArgonHalf<int16_t> rshiftedB = 0;
	Argon<uint32_t> readValueA = 0U;
	Argon<uint32_t> readValueB = 0U;

	int32_t rshiftAmount = (32 - tableSizeMagnitude - 16);

	util::constexpr_for<0, 4, 1>([&]<int i>() {
		{
			phase += phaseIncrement;
			rshiftedA[i] = phase >> rshiftAmount;

			uint32_t whichValue = phase >> (32 - tableSizeMagnitude);
			uint32_t* readAddress = (uint32_t*)((uint32_t)table + (whichValue << 1));
			readValueA[i].Load(readAddress);
		}

		{
			uint32_t phaseLater = phase + phaseToAdd;
			rshiftedB[i] = phaseLater >> rshiftAmount;

			uint32_t whichValue = phaseLater >> (32 - tableSizeMagnitude);
			uint32_t* readAddress = (uint32_t*)((uint32_t)table + (whichValue << 1));
			readValueB[i].Load(readAddress);
		}
	});

	ArgonHalf<int16_t> valueA1 = readValueA.Narrow().As<int16_t>();
	ArgonHalf<int16_t> valueA2 = readValueA.ShiftRightNarrow<16>().As<int16_t>();

	ArgonHalf<int16_t> valueB1 = readValueB.Narrow().As<int16_t>();
	ArgonHalf<int16_t> valueB2 = readValueB.ShiftRightNarrow<16>().As<int16_t>();

	/* Sneakily do this backwards to flip the polarity of the output, which we need to do anyway */
	ArgonHalf<int16_t> strengthA1 = rshiftedA | std::numeric_limits<int16_t>::min();
	ArgonHalf<int16_t> strengthA2 = std::numeric_limits<int16_t>::min() - strengthA1;

	Argon<int32_t> outputA = strengthA2.MultiplyDoubleSaturateLong(valueA2);
	outputA = outputA.MultiplyDoubleAddSaturateLong(strengthA1, valueA1);

	ArgonHalf<int16_t> strengthB2 = rshiftedB & std::numeric_limits<int16_t>::max();
	ArgonHalf<int16_t> strengthB1 = std::numeric_limits<int16_t>::max() - strengthB2;

	Argon<int32_t> outputB = strengthB2.MultiplyDoubleSaturateLong(valueB2);
	outputB = outputB.MultiplyDoubleAddSaturateLong(strengthB1, valueB1);

	auto output = outputA.MultiplyRoundFixedPoint(outputB) << 1; // (a *. b) << 1
	return {output, phase};
}
