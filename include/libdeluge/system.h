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

/// Emit debug/log text to the board's debug channel (RTT/UART), with no implicit
/// newline — the caller owns formatting. Best-effort, may be dropped. [task] [isr]
void deluge_log(const char* text);

/// Enter a critical section: mask interrupts so the following code runs without
/// preemption. Nestable — each ENTER must be paired with one EXIT, and only the
/// outermost pair actually toggles interrupts (a shared depth counter tracks the
/// nesting). Must be balanced. Prefer the RAII CriticalSectionGuard from C++.
///
/// This is a CPU/system primitive owned by the BSP (the RZ/A1L implementation
/// masks IRQs via CPSID/CPSIE; a host-sim would use a recursive lock or no-op;
/// Embassy would use a critical-section crate). [task] [isr]
void ENTER_CRITICAL_SECTION();

/// Exit a critical section opened by ENTER_CRITICAL_SECTION(). [task] [isr]
void EXIT_CRITICAL_SECTION();

#ifdef __cplusplus
}

/// RAII wrapper for ENTER/EXIT_CRITICAL_SECTION — enters on construction, exits
/// on scope end, so the pairing can't be forgotten.
struct CriticalSectionGuard {
	CriticalSectionGuard() { ENTER_CRITICAL_SECTION(); }
	~CriticalSectionGuard() { EXIT_CRITICAL_SECTION(); }
};
#endif

#endif // LIBDELUGE_SYSTEM_H
