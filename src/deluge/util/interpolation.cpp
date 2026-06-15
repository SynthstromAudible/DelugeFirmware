/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "util/interpolation.h"
#include "util/lookuptables/lookuptables.h" // decayTableSmall4/8

int32_t interpolateTable(uint32_t input, int32_t numBitsInInput, const uint16_t* table, int32_t numBitsInTableSize) {
	int32_t whichValue = input >> (numBitsInInput - numBitsInTableSize);
	int32_t value1 = table[whichValue];
	int32_t value2 = table[whichValue + 1];

	int32_t rshiftAmount = numBitsInInput - 15 - numBitsInTableSize;
	uint32_t rshifted;
	if (rshiftAmount >= 0) {
		rshifted = input >> rshiftAmount;
	}
	else {
		rshifted = input << (-rshiftAmount);
	}

	int32_t strength2 = rshifted & 32767;
	int32_t strength1 = 32768 - strength2;
	return value1 * strength1 + value2 * strength2;
}

uint32_t interpolateTableInverse(int32_t tableValueBig, int32_t numBitsInLookupOutput, const uint16_t* table,
                                 int32_t numBitsInTableSize) {
	int32_t tableValue = tableValueBig >> 15;
	int32_t tableSize = 1 << numBitsInTableSize;

	int32_t tableDirection = (table[0] < table[tableSize]) ? 1 : -1;

	// Check we're not off either end of the table
	if ((tableValue - table[0]) * tableDirection <= 0) {
		return 0;
	}
	if ((tableValue - table[tableSize]) * tableDirection >= 0) {
		return (1 << numBitsInLookupOutput) - 1;
	}

	int32_t rangeStart = 0;
	int32_t rangeEnd = tableSize;

	while (rangeStart + 1 < rangeEnd) {

		int32_t examinePos = (rangeStart + rangeEnd) >> 1;
		if ((tableValue - table[examinePos]) * tableDirection >= 0) {
			rangeStart = examinePos;
		}
		else {
			rangeEnd = examinePos;
		}
	}

	uint32_t output = rangeStart << (numBitsInLookupOutput - numBitsInTableSize);
	output += (uint64_t)(tableValueBig - ((uint16_t)table[rangeStart] << 15))
	          * (1 << (numBitsInLookupOutput - numBitsInTableSize))
	          / (((uint16_t)table[rangeStart + 1] - (uint16_t)table[rangeStart]) << 15);

	return output;
}

int32_t getDecay8(uint32_t input, uint8_t numBitsInInput) {
	return interpolateTable(input, numBitsInInput, decayTableSmall8);
}

int32_t getDecay4(uint32_t input, uint8_t numBitsInInput) {
	return interpolateTable(input, numBitsInInput, decayTableSmall4);
}
