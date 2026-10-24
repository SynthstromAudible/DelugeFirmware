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

#pragma once

/// foundation/panic.h — the panic / fatal-error primitive.
///
/// Part of the foundation tier, which sits *below* the application, the BSP and
/// the HAL: every layer may panic. Foundation headers live under src/ so they
/// resolve through the universal `-I src` include path every target already has
/// (this is also why definitions.h — included by vendored libs that lack the
/// libdeluge boundary path — can pull it in).
///
/// A panic is the one accepted cross-cutting "handler" (cf. Rust's
/// `#[panic_handler]`): it is terminal, not ongoing control flow, so it does not
/// violate the no-upcall rule the same way an event callback would. The pieces:
///   - freezeWithError(): the terminal handler — report the message and halt. The
///     Deluge application provides the rich OLED renderer (hid/display); a
///     host-sim/other board provides its own.
///   - fault_handler_*(): dump CPU fault context to the board's status LEDs — the
///     hardware side of the panic system, provided by the board.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Terminal panic: report `errmsg` and halt. Does not return in practice.
extern void freezeWithError(char const* errmsg);

typedef enum : uint32_t {
	// ARM fault types
	UNDEFINED = 0,
	PREFETCH = 1,
	ABORT = 2,
	RESERVED = 3,
	// others
	SOFT_FAULT = 0xE, // freeze_with_error
	SVC = 0xF,        // theoretically this is only possible by deliberately making a syscall
} cpu_fault_type;

/// Dump fault context (link + stack pointers) to the board's status LEDs. Called
/// from FREEZE_WITH_ERROR; handle_cpu_fault() is the CPU fault vector's entry.
void fault_handler_print_freeze_pointers(uint32_t addrUSRLR, uint32_t addrUSRSP);
void handle_cpu_fault(uint32_t exception_lr, cpu_fault_type exception_type, uint32_t sys_lr, uint32_t sys_sp);

#ifdef __cplusplus
}
#endif

#if defined(__arm__)
#define FREEZE_WITH_ERROR(error)                                                                                       \
	({                                                                                                                 \
		uint32_t regLR = 0;                                                                                            \
		uint32_t regSP = 0;                                                                                            \
		asm volatile("MOV %0, LR\n" : "=r"(regLR));                                                                    \
		asm volatile("MOV %0, SP\n" : "=r"(regSP));                                                                    \
		fault_handler_print_freeze_pointers(regLR, regSP);                                                             \
		freezeWithError(error);                                                                                        \
	})
#else
#define FREEZE_WITH_ERROR(error) ({ freezeWithError(error); })
#endif
