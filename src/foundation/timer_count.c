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

#include "foundation/timer_count.h"
#include "board_config.h" // XTAL_SPEED_MHZ

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
