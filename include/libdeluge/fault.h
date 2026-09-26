/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// libdeluge/fault.h — what the crash reporter needs from the board.
///
/// The pad-grid crash pattern itself is portable and lives in the application
/// (`src/deluge/io/debug/fault_pattern.c`): it encodes each candidate return address
/// as 32 lit pads so a photograph of the panel can be decoded back into a
/// backtrace. Only two things about it are board-specific — how bytes reach the pad
/// driver, and which address ranges count as code or stack — and those are the hooks
/// below.
///
/// **Every hook here runs from a CPU fault vector**, with the scheduler dead, most
/// state untrustworthy and quite possibly a corrupted heap. They must therefore be
/// synchronous, allocation-free and interrupt-free: no async runtime, no waiting on
/// a task, no DMA completion interrupt. A hook that needs the hardware taken away
/// from a driver should simply take it — nothing is going to run afterwards.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Address ranges the crash reporter classifies fault-time pointers against, so it
/// can tell a plausible return address from ordinary data while walking the stack.
typedef struct DelugeFaultRanges {
	/// Executable image bounds, `[code_start, code_end)`.
	uintptr_t code_start;
	uintptr_t code_end;
	/// Program (application) stack bounds, `[stack_start, stack_end)`.
	uintptr_t stack_start;
	uintptr_t stack_end;
	/// @brief Optional second stack, `[alt_stack_start, alt_stack_end)`; zeroed if the board has none.
	///
	/// A board may run application code on a stack other than the program stack — the Rust BSP's
	/// storage worker fiber had its own, in a different memory region, until it was retired; no board
	/// has one now. The pointer walk is gated on the faulting SP falling inside a known stack, so a
	/// fault on such a stack would produce a single address (the link register) and no call chain.
	uintptr_t alt_stack_start;
	uintptr_t alt_stack_end;
} DelugeFaultRanges;

/// Fill in the board's code and stack bounds. Must not fail; zeroed fields simply
/// mean "cannot classify", which costs the report some candidate pointers.
void deluge_fault_ranges(DelugeFaultRanges* out);

/// Queue one byte to the pad driver on the fault path.
///
/// Byte-at-a-time deliberately: it mirrors how the pattern is generated (a colour at
/// a time, a column-pair at a time) and keeps the hook trivial enough to be
/// obviously correct in a fault context. Call cost is irrelevant here — nothing else
/// is going to run.
void deluge_fault_pad_write(uint8_t byte);

/// Push every queued byte to the pad driver and block until the wire is idle.
///
/// Must not rely on an interrupt or a DMA completion callback to make progress —
/// poll. Must also be bounded: a wedged transmitter has to lose the race rather than
/// hang the crash report, since the report is the only thing left that can tell
/// anyone what happened.
void deluge_fault_pad_flush(void);

#ifdef __cplusplus
}
#endif
