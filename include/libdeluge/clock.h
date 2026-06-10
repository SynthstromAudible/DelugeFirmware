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

/// Rate of the free-running counter read by `deluge_clock_now`, in ticks per
/// second. [task] [audio] [isr]
uint64_t deluge_clock_ticks_per_second(void);

/// Current value of the board's free-running high-resolution counter. Suitable
/// for short-interval timing and entropy. NOTE: on boards whose hardware counter
/// is narrow (e.g. the RZ/A1L's 16-bit MTU), this wraps — it is not yet a wide
/// monotonic clock; callers doing interval math should difference within the
/// counter width. [task] [audio] [isr]
uint64_t deluge_clock_now(void);

/// Wide (64-bit) monotonic tick count since boot — the board accumulates any
/// hardware-counter rollover, so it does not wrap in practice. The unit is
/// `deluge_clock_monotonic_hz()` ticks/second. This is the absolute clock the
/// scheduler runs on; for short-interval timing / entropy use deluge_clock_now().
///
/// MUST be read from a single (cooperative-task) context: the board may extend a
/// narrower hardware counter using internal rollover state that assumes serialized
/// reads frequent enough never to miss a wrap. [task]
uint64_t deluge_clock_monotonic(void);

/// Rate of deluge_clock_monotonic() in ticks/second. [task]
uint64_t deluge_clock_monotonic_hz(void);

/// Busy-wait for approximately `us` microseconds. Use sparingly; prefer the
/// scheduler for anything non-trivial. [task]
void deluge_clock_delay_us(uint32_t us);

/// Busy-wait for approximately `ms` milliseconds. Separate from the microsecond
/// path because boards may use a coarser timer for longer delays. [task]
void deluge_clock_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CLOCK_H
