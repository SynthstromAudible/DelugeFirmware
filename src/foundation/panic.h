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
///   - fault_handler_print_freeze_pointers() / deluge_cpu_fault_report(): draw the
///     freeze's or fault's call chain on the pads (io/debug/fault_pattern.c), through
///     the board's libdeluge/fault.h hooks. The board's fault vectors call the latter.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Terminal panic: report `errmsg` and halt. Does not return in practice.
extern void freezeWithError(char const* errmsg);

/// @brief What stopped the CPU, as the fault vectors and FREEZE_WITH_ERROR report it.
///
/// The values are the HAL's: its fault vectors pass them in a register, so they must not change.
typedef enum DelugeCpuFault : uint32_t {
	DELUGE_CPU_FAULT_UNDEFINED = 0,      ///< Undefined instruction.
	DELUGE_CPU_FAULT_PREFETCH_ABORT = 1, ///< Instruction fetch from an address that can't be executed.
	DELUGE_CPU_FAULT_DATA_ABORT = 2,     ///< Load or store to an address that can't be accessed.
	DELUGE_CPU_FAULT_RESERVED = 3,       ///< The reserved vector (unreachable on the Cortex-A9).
	DELUGE_CPU_FAULT_RUST_PANIC = 0xD,   ///< A Rust panic in the board support, not a CPU exception.
	DELUGE_CPU_FAULT_FREEZE = 0xE,       ///< FREEZE_WITH_ERROR, not a CPU exception.
	DELUGE_CPU_FAULT_SVC = 0xF,          ///< Supervisor call; nothing makes them, so it is a fault.
} DelugeCpuFault;

/// @brief Draw FREEZE_WITH_ERROR's call site and call chain on the pads.
/// @param lr The return address of the function that froze.
/// @param sp The stack pointer at the freeze, where the call chain is walked from.
void fault_handler_print_freeze_pointers(uint32_t lr, uint32_t sp);

/// @brief The board's CPU fault vectors hand over here: draw the fault on the pads and halt.
/// @param fault_pc   Address of the faulting instruction.
/// @param fault_type Which exception it was.
/// @param sys_lr     The interrupted code's link register: the return address of the call that went wrong.
/// @param sys_sp     The interrupted code's stack pointer, where the call chain is walked from.
[[noreturn]] void deluge_cpu_fault_report(uint32_t fault_pc, DelugeCpuFault fault_type, uint32_t sys_lr,
                                          uint32_t sys_sp);

#ifdef __cplusplus
}
#endif

// The pad-grid fault report needs the board (its transport and code ranges), so a host build freezes without it, even
// when compiled for ARM.
#if defined(__arm__) && !defined(DELUGE_HOST)
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
