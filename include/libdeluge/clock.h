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
/// One free-running tick counter at a board-defined rate. The application's
/// `Time`/`DELUGE_CLOCKS_PER` helpers can be expressed on top of this.
#ifndef LIBDELUGE_CLOCK_H
#define LIBDELUGE_CLOCK_H

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Current value of the board's free-running high-resolution counter. Suitable
/// for short-interval timing and entropy. NOTE: on boards whose hardware counter
/// is narrow (e.g. the RZ/A1L's 16-bit MTU), this wraps — it is not yet a wide
/// monotonic clock; callers doing interval math should difference within the
/// counter width. [task] [audio] [isr]
uint64_t deluge_clock_now(void) DELUGE_NOEXCEPT;

/// The CPU's free-running cycle counter: 32 bits, wrapping (about 10.8 s on the device), so difference two readings
/// within that. For measuring short spans of work, such as a block's render or one Sound's voices. Reads 0 where
/// the board has no cycle counter (see deluge_cycle_hz). [task] [audio] [isr]
uint32_t deluge_cycle_count(void) DELUGE_NOEXCEPT;

/// The CPU cycles spent on the caller's work: deluge_cycle_count less the time the USB interrupt has taken, which
/// otherwise counts in whatever it interrupts. 32 bits, wrapping; difference two readings. For the cost of a piece of
/// work, such as a Sound's voices; a span's share of real time is deluge_cycle_count's. Reads 0 where the board has no
/// cycle counter. Not for an interrupt handler. [task] [audio]
uint32_t deluge_work_cycle_count(void) DELUGE_NOEXCEPT;

/// The rate deluge_cycle_count counts at, in Hz; 0 where there is no cycle counter (the host), which callers read
/// as "nothing measured". [task] [audio] [isr]
uint32_t deluge_cycle_hz(void) DELUGE_NOEXCEPT;

/// Busy-wait for approximately `us` microseconds. Use sparingly; prefer the
/// scheduler for anything non-trivial. [task]
void deluge_clock_delay_us(uint32_t us) DELUGE_NOEXCEPT;

/// Busy-wait for approximately `ms` milliseconds. Separate from the microsecond
/// path because boards may use a coarser timer for longer delays. [task]
void deluge_clock_delay_ms(uint32_t ms) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CLOCK_H
