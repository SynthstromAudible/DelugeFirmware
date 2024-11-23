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
#include <chrono>
#include <time.h>
extern "C" {
#include "OSLikeStuff/timers_interrupts/clock_type.h"
#include "RZA1/ostm/ostm.h"
/* clock_t, clock, CLOCKS_PER_SEC */
#define clockConversion DELUGE_CLOCKS_PER / CLOCKS_PER_SEC;

std::chrono::time_point<std::chrono::steady_clock> timers[2];
uint32_t mockTimers[2]{0};
// variable in case some tests need to use the system clock in the future
bool mockTimeIntervals = true;

void enableTimer(int timerNo) {
	auto begin = std::chrono::steady_clock::now();
	mockTimers[timerNo] = 0;
	timers[timerNo] = begin;
}

void disableTimer(int timerNo) {
}

bool isTimerEnabled(int timerNo) {
	return true;
}

void setOperatingMode(int timerNo, enum OSTimerOperatingMode mode, bool enable_interrupt) {
}

void setTimerValue(int timerNo, uint32_t timerValue) {
	auto begin = std::chrono::steady_clock::now();
	timers[timerNo] = begin;
	mockTimers[timerNo] = timerValue;
}

void passMockTime(double seconds) {
	uint32_t ticks = seconds * DELUGE_CLOCKS_PER;
	mockTimers[0] += ticks;
	mockTimers[1] += ticks;
}

// returns ticks at the rate the deluge clock would generate them
uint32_t getTimerValue(int timerNo) {
	/// this ensures that the mocked time keeps advancing
	passMockTime(0.0000001);
	auto now = std::chrono::steady_clock::now();
	if (mockTimeIntervals) {
		return mockTimers[timerNo];
	}
	return DELUGE_CLOCKS_PER * ((std::chrono::duration<double>(now - timers[0]).count()));
}
}
