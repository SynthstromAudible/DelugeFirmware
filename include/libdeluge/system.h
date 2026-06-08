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

/// libdeluge/system.h — platform lifecycle and low-level services.
///
/// Replaces the boot/reset/fault wiring currently in `main.c`, `resetprg.c`,
/// `fault_handler`, and `RZA1/intc`. The platform calls `deluge_platform_init`
/// before any other service and before `deluge_app_init`.
#ifndef LIBDELUGE_SYSTEM_H
#define LIBDELUGE_SYSTEM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Bring up the SoC and board to the point where all other libdeluge services
/// are usable (clocks, MMU/cache, interrupt controller, peripherals). Must be
/// called exactly once, first. [task]
DelugeStatus deluge_platform_init(void);

/// Reset the device. Does not return. [task]
void deluge_system_reset(void) __attribute__((noreturn));

/// Stop all hardware timers/peripherals in preparation for handing control away
/// (e.g. chainloading new firmware). Call with interrupts already disabled. [task]
void deluge_system_quiesce(void);

/// Emit a line of debug/log text (RTT/UART). Best-effort, may be dropped. [task] [isr]
void deluge_log(const char* text);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_SYSTEM_H
