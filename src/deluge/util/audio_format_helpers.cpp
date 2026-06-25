/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "util/audio_format_helpers.h"
#include <cmath>
#include <cstdint>

int32_t memToUIntOrError(char const* __restrict__ mem, char const* const memEnd) {
	uint32_t number = 0;
	while (mem != memEnd) {
		if (*mem < '0' || *mem > '9') {
			return -1;
		}
		number *= 10;
		number += (*mem - '0');
		mem++;
	}

	return number;
}

#define UnsignedToFloat(u) (((double)((long)(u - 2147483647L - 1))) + 2147483648.0)

double ConvertFromIeeeExtended(unsigned char* bytes /* LCN */) {
	double f;
	int32_t expon;
	unsigned long hiMant, loMant;

	expon = ((bytes[0] & 0x7F) << 8) | (bytes[1] & 0xFF);
	hiMant = ((unsigned long)(bytes[2] & 0xFF) << 24) | ((unsigned long)(bytes[3] & 0xFF) << 16)
	         | ((unsigned long)(bytes[4] & 0xFF) << 8) | ((unsigned long)(bytes[5] & 0xFF));
	loMant = ((unsigned long)(bytes[6] & 0xFF) << 24) | ((unsigned long)(bytes[7] & 0xFF) << 16)
	         | ((unsigned long)(bytes[8] & 0xFF) << 8) | ((unsigned long)(bytes[9] & 0xFF));

	if (expon == 0 && hiMant == 0 && loMant == 0) {
		f = 0;
	}
	else {
		if (expon == 0x7FFF) { /* Infinity or NaN */
			f = HUGE_VAL;
		}
		else {
			expon -= 16383;
			f = ldexp(UnsignedToFloat(hiMant), expon -= 31);
			f += ldexp(UnsignedToFloat(loMant), expon -= 32);
		}
	}

	if (bytes[0] & 0x80) {
		return -f;
	}
	else {
		return f;
	}
}
