/*
 * Copyright © 2024 Mark Adams
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
#include "RZA1/ostm/ostm.h"
#include <time.h> /* clock_t, clock, CLOCKS_PER_SEC */
#define clockConversion DELUGE_CLOCKS_PER / CLOCKS_PER_SEC;
clock_t timers[2] = {0, 0};

void enableTimer(int timerNo) {
	timers[timerNo] = clock();
}

void disableTimer(int timerNo) {
}

bool isTimerEnabled(int timerNo) {
	return true;
}

void setOperatingMode(int timerNo, enum OSTimerOperatingMode mode, bool enable_interrupt) {
}

void setTimerValue(int timerNo, uint32_t timerValue) {
	timers[timerNo] = clock();
}

// returns ticks at the rate the deluge clock would generate them
uint32_t getTimerValue(int timerNo) {
	return (clock() - timers[timerNo]) * clockConversion;
}

float getTimerValueSeconds(int timerNo) {
	float seconds = ((float)getTimerValue(timerNo) / DELUGE_CLOCKS_PERf);
	return seconds;
}
