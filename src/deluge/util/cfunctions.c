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
