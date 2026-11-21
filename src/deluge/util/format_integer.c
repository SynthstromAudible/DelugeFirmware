/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "util/cfunctions.h"

int32_t getNumDecimalDigits(uint32_t number) {
	uint32_t size_test = 10;
	int32_t total_num_digits = 1;
	while (number >= size_test) {
		total_num_digits++;
		if (total_num_digits == 10) {
			break;
		}
		size_test *= 10;
	}
	return total_num_digits;
}

void intToString(int32_t number, char* __restrict__ buffer, int32_t minNumDigits) {

	int32_t is_negative = (number < 0);
	if (is_negative) {
		number =
		    (int32_t)((uint32_t)0
		              - (uint32_t)
		                  number); // Can't just go "-number", cos somehow that doesn't work for negative 2 billion!
	}

	int32_t total_num_digits = getNumDecimalDigits(number);

	if (total_num_digits < minNumDigits) {
		total_num_digits = minNumDigits;
	}

	if (is_negative) {
		total_num_digits++;
		buffer[0] = '-';
	}

	buffer[total_num_digits] = 0;
	int32_t char_pos = total_num_digits - 1;
	while (1) {
		if (char_pos == is_negative) {
			buffer[char_pos] = (char)('0' + number);
			break;
		}
		int32_t divided = (int32_t)((uint32_t)number / 10);
		buffer[char_pos--] = (char)('0' + (number - (divided * 10)));
		number = divided;
	}
}
