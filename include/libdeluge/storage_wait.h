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

/// libdeluge/storage_wait.h — cooperative wait hook for slow storage I/O.
///
/// While the BSP (and the RZ/A1L HAL it wraps) busy-waits on slow storage
/// peripherals — SD/MMC and SPI flash — it must hand the CPU back to whoever
/// owns it so audio, USB and UI keep running. These hooks are *provided by the
/// runtime* (the application or RTOS) and *called by the BSP*: the BSP passes a
/// `RunCondition` describing *what hardware state it is waiting for* and the
/// runtime yields until that holds. Expressing the wait as a C-ABI predicate
/// (rather than a direct call into the task scheduler) is what lets the BSP stay
/// free of the task layer.
///
/// On the Deluge the runtime yields to the cooperative scheduler, which keeps the
/// registered audio/USB/UI tasks running; on a host-sim it can spin an event loop;
/// on Embassy each maps onto an `await` (`poll_fn` on the condition / `Timer`).
/// There is deliberately no conditionless "pump" form: every wait names its
/// condition, so it ports to async unchanged.
///
/// Despite the historical "SD" names, these fire for any slow-storage busy-wait.
#ifndef LIBDELUGE_STORAGE_WAIT_H
#define LIBDELUGE_STORAGE_WAIT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Predicate the BSP polls while yielding; returns true once the wait can end.
typedef bool (*RunCondition)();

/// Yield to the runtime until `until` returns true. [task]
void yieldingRoutineForSD(RunCondition until);

/// Yield to the runtime until `until` returns true or `timeoutSeconds` elapses.
/// Returns true if `until` was satisfied, false on timeout. [task]
bool yieldingRoutineWithTimeoutForSD(RunCondition until, double timeoutSeconds);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_STORAGE_WAIT_H
