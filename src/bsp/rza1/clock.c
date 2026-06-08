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

/// RZ/A1L implementation of <libdeluge/clock.h>.
///
/// Part of bsp-rza1-legacy: the BSP is the only consumer of the HAL, so it may
/// include RZA1/* headers. The busy-wait bodies below are relocated verbatim
/// from the application's util/cfunctions.c (delayMS/delayUS) so behaviour is
/// byte-for-byte unchanged; only the layer they live in moved.
///
/// Not yet implemented here (added when first consumed by the application):
/// deluge_clock_now() and deluge_clock_ticks_per_second(), which need a
/// rollover-tracked monotonic counter beyond the 16-bit MTU TCNT.

#include "libdeluge/clock.h"

#include "RZA1/mtu/mtu.h"
#include "definitions.h"

/// Counts of TIMER_SYSTEM_SLOW (P0/prescaler) per millisecond.
/// Matches util/cfunctions.c::msToSlowTimerCount.
static uint32_t ms_to_slow_timer_count(uint32_t ms) {
	return ms * 33;
}

/// Counts of TIMER_SYSTEM_FAST per microsecond.
/// Matches util/cfunctions.c::usToFastTimerCount.
static uint32_t us_to_fast_timer_count(uint32_t us) {
	return (uint64_t)us * XTAL_SPEED_MHZ / 25600000;
}

void deluge_clock_delay_ms(uint32_t ms) {
	uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];
	uint16_t stopTime = startTime + ms_to_slow_timer_count(ms);
	while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - stopTime) >= 8) {
		;
	}
}

void deluge_clock_delay_us(uint32_t us) {
	uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t stopTime = startTime + us_to_fast_timer_count(us);
	while ((int16_t)(*TCNT[TIMER_SYSTEM_FAST] - stopTime) < 0) {
		;
	}
}
