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
#include "RZA1/mtu/mtu.h"
#include "definitions.h"
#include <string.h>

int32_t getNumDecimalDigits(uint32_t number) {
	uint32_t sizeTest = 10;
	int32_t totalNumDigits = 1;
	while (number >= sizeTest) {
		totalNumDigits++;
		if (totalNumDigits == 10) {
			break;
		}
		sizeTest *= 10;
	}
	return totalNumDigits;
}

void intToString(int32_t number, char* __restrict__ buffer, int32_t minNumDigits) {

	int32_t isNegative = (number < 0);
	if (isNegative) {
		number = (uint32_t)0
		         - (uint32_t)number; // Can't just go "-number", cos somehow that doesn't work for negative 2 billion!
	}

	int32_t totalNumDigits = getNumDecimalDigits(number);

	if (totalNumDigits < minNumDigits) {
		totalNumDigits = minNumDigits;
	}

	if (isNegative) {
		totalNumDigits++;
		buffer[0] = '-';
	}

	buffer[totalNumDigits] = 0;
	int32_t charPos = totalNumDigits - 1;
	while (1) {
		if (charPos == isNegative) {
			buffer[charPos] = '0' + number;
			break;
		}
		int32_t divided = (uint32_t)number / 10;
		buffer[charPos--] = '0' + (number - (divided * 10));
		number = divided;
	}
}

void floatToString(float number, char* __restrict__ buffer, int32_t minNumDecimalPlaces, int32_t maxNumDecimalPlaces) {

	uint32_t rawBinary = *(uint32_t*)&number;
	char* writePos = buffer;

	if (rawBinary >> 31) { // If negative...
		*writePos = '-';
		writePos++;
	}

	char* leftmostDigitPos = writePos; // Not including minus sign.

	int32_t exponent = (int32_t)((rawBinary >> 23) & 255) - 127;

	if (exponent >= 0) {

		uint32_t leftOfDecimals = (rawBinary & 0x007FFFFF) | 0x00800000;
		if (exponent <= 23) {
			leftOfDecimals >>= (23 - exponent);
		}
		else if (exponent <= 30) { // Could go up to 31 if we had an unsignedIntToString().
			leftOfDecimals <<= (exponent - 23);
		}
		else {
			*(writePos++) = 'i';
			*(writePos++) = 'n';
			*(writePos++) = 'f';
			*writePos = 0;
			return;
		}
		intToString(leftOfDecimals, writePos, 1);
		writePos = strchr(writePos, 0);
	}
	else {
		*(writePos++) = '0';
	}

	uint32_t fractionRemaining = rawBinary & 0x007FFFFF;
	fractionRemaining |= 0x00800000; // Draw in the invisible "1".
	if (exponent >= -5) {
		fractionRemaining <<= (exponent + 5);
	}
	else {
		fractionRemaining >>= (-5 - exponent); // Yes, in this case, we throw away bits and lose accuracy.
	}

	int32_t decimalPlace = 0;

	while (true) {
		fractionRemaining &= 0x0FFFFFFF;

		if (!fractionRemaining && decimalPlace >= minNumDecimalPlaces) {
			*writePos = 0;
			return;
		}

		if (decimalPlace >= maxNumDecimalPlaces) {
			break;
		}

		if (!decimalPlace) {
			*writePos = '.';
			writePos++;
		}

		fractionRemaining *= 10;
		int32_t decimalDigit = fractionRemaining >> 28;
		*writePos = '0' + decimalDigit;
		writePos++;
		decimalPlace++;
	}

	// We've reached our max number of decimal places, but still got a remainder.
	*writePos = 0;
	char* oldEndPos;

	// See if we need to round up.
	if (fractionRemaining >= (uint32_t)0x08000000) {
		oldEndPos = writePos;
moveBackOneDigit:
		// If we reached left of string, oh no, we can't move back any further. So move everything else instead.
		if (writePos == leftmostDigitPos) {
			goto needExtraDigitOnLeft;
		}

		writePos--;
		decimalPlace--;
		if (*writePos == '.') {
			if (minNumDecimalPlaces <= 0) {
				*writePos = 0;
				oldEndPos = writePos; // Update it
			}
			writePos--;
		}
		if (*writePos == '9') {
			if (decimalPlace >= minNumDecimalPlaces) {
				*writePos = 0;
				oldEndPos = writePos; // Update it
			}
			else {
				*writePos = '0';
			}
			goto moveBackOneDigit;
		}
		(*writePos)++; // Increment that digit. Not moving the pointer.
	}

	// Or if not rounding up, we still may have a string of zeros on the end, which may go above our min decimal places,
	// so we should lose those.
	else {
		while (true) {
			writePos--;
			if (decimalPlace <= minNumDecimalPlaces) {
				if (*writePos == '.') {
					*writePos =
					    0; // If min decimal places was 0 and we got that far back, get rid of the decimal point.
				}
				break;
			}
			decimalPlace--;
			if (*writePos != '0') {
				break;
			}
			*writePos = 0;
		}
	}
	return;

	// If we reached left of string, oh no, we can't move back any further. So move everything else instead.
needExtraDigitOnLeft: {}
	char* readPos = oldEndPos;
	while (readPos >= leftmostDigitPos) {
		*(readPos + 1) = *readPos;
		readPos--;
	}
	*leftmostDigitPos = '1';
}

void slotToString(int32_t slot, int32_t subSlot, char* __restrict__ buffer, int32_t minNumDigits) {
	intToString(slot, buffer, minNumDigits);

	if (subSlot != -1) {
		int32_t stringLength = strlen(buffer);
		buffer[stringLength] = subSlot + 'A';
		buffer[stringLength + 1] = 0;
	}
}

uint32_t superfastTimerCountToNS(uint32_t timerCount) {
	return (uint64_t)timerCount * 400000000 / XTAL_SPEED_MHZ;
}

uint32_t superfastTimerCountToUS(uint32_t timerCount) {
	return (uint64_t)timerCount * 400000 / XTAL_SPEED_MHZ;
}

uint32_t fastTimerCountToUS(uint32_t timerCount) {
	return (uint64_t)timerCount * 25600000 / XTAL_SPEED_MHZ;
}

uint32_t usToFastTimerCount(uint32_t us) {
	return (uint64_t)us * XTAL_SPEED_MHZ / 25600000;
}

uint32_t msToSlowTimerCount(uint32_t ms) {
	return ms * 33;
}

void delayMS(uint32_t ms) {
	uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];
	uint16_t stopTime = startTime + msToSlowTimerCount(ms);
	while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - stopTime) >= 8) {
		;
	}
}

void delayUS(uint32_t us) {
	uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t stopTime = startTime + usToFastTimerCount(us);
	while ((int16_t)(*TCNT[TIMER_SYSTEM_FAST] - stopTime) < 0) {
		;
	}
}
