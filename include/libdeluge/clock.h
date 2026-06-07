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

/// libdeluge/clock.h — monotonic high-resolution time.
///
/// One free-running tick counter at a board-defined rate. Replaces direct use of
/// the RZ/A1L MTU/OSTM timers (`drivers/mtu`, `RZA1/ostm`). The application's
/// `Time`/`DELUGE_CLOCKS_PER` helpers can be expressed on top of this.
#ifndef LIBDELUGE_CLOCK_H
#define LIBDELUGE_CLOCK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Ticks of the monotonic clock per second. [task] [audio] [isr]
uint64_t deluge_clock_ticks_per_second(void);

/// Current value of the free-running monotonic tick counter. Never goes
/// backwards. [task] [audio] [isr]
uint64_t deluge_clock_now(void);

/// Busy-wait for approximately `us` microseconds. Use sparingly; prefer the
/// scheduler for anything non-trivial. [task]
void deluge_clock_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CLOCK_H
