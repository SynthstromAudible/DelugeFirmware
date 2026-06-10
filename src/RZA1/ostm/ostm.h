/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#ifndef RZA1_OSTM_OSTM_H_
#define RZA1_OSTM_OSTM_H_

#include "RZA1/system/r_typedefs.h"

/// refer to
/// https://www.renesas.com/us/en/document/mah/rza1l-group-rza1lu-group-rza1lc-group-users-manual-hardware?r=1054491#G14.1027450
// ticks at 33.33 MHz

enum OSTimerOperatingMode { TIMER, FREE_RUNNING };

// NOTE: these are deliberately ostm*-prefixed. The MTU2 driver (RZA1/mtu/mtu.h)
// also declares enableTimer()/disableTimer()/isTimerEnabled() — as `static inline`
// functions on entirely different hardware. A translation unit that includes both
// headers (e.g. src/bsp/rza1/clock.c, which needs MTU for delays and OSTM for the
// monotonic clock) would silently bind the unprefixed names to the MTU versions,
// leaving the OSTM timer configured but never started. Keep the prefix.

/// in timer mode, start or reset the timer
/// in free mode start the timer iff it's not running
void ostmEnableTimer(int timerNo);

/// stop the timer
void ostmDisableTimer(int timerNo);

/// return whether the timer is running
bool ostmIsTimerEnabled(int timerNo);

/// The timer can be a timer, starting at OSTMnCMP and counting down to 0 then optionally sending an interrupt, or a
/// free running loop with an optional interrupt when it equals OSTMnCMP. Count is driven by P0 (33.33MHz)
void ostmSetOperatingMode(int timerNo, enum OSTimerOperatingMode mode, bool enable_interrupt);

/// Count is driven by P0 (33.33MHz)
void ostmSetTimerValue(int timerNo, uint32_t timerValue);

uint32_t ostmGetTimerValue(int timerNo);
#endif // RZA1_OSTM_OSTM_H_
