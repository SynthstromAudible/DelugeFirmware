/*
 * Copyright © 2015-2025 Synthstrom Audible Limited
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

#include "memory/stack_guard.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include <cstdint>

// NOLINTBEGIN — addresses are only meaningful via &symbol
extern uint32_t program_stack_start;
extern uint32_t program_stack_end;
// NOLINTEND

namespace {
int32_t closestDistance = 2147483647;
} // namespace

void checkStack(char const* caller) {
#if ALPHA_OR_BETA_VERSION

	// The stack-collision guard measures headroom between the live stack pointer and
	// `program_stack_start` — valid only where the stack sits in a known region just past
	// the heap (the SoC's SRAM layout). A build whose BSP can't describe such a region
	// leaves `program_stack_end == program_stack_start` (e.g. the host sim, which runs on
	// the OS-managed native stack — an unrelated, far-away address). There the subtraction
	// underflows to a huge bogus "distance" and would false-trigger E338; the OS guard page
	// already protects that build, so skip.
	if (&program_stack_start == &program_stack_end) {
		return;
	}

	char a;

	// The collision guard measures headroom against `program_stack_start`, which is
	// valid ONLY while running on the main program stack. On the Rust/Embassy BSP the
	// audio render also runs from the audio interrupt-executor, which executes in SYS
	// mode on the *preempted* context's stack — e.g. the worker fiber's SDRAM stack
	// during a song load. There `&a` lies far outside [program_stack_start,
	// program_stack_end], so the distance is meaningless and would false-trigger E338
	// every render (logged as a huge/negative "bytes in stack" + COLLISION, then a
	// freezeWithError blit from interrupt context that corrupts the display). Only run
	// the guard when the live SP is actually on the main stack.
	uintptr_t sp = (uintptr_t)&a;
	if (sp < (uintptr_t)&program_stack_start || sp > (uintptr_t)&program_stack_end) {
		return;
	}

	int32_t distance = (int32_t)((uintptr_t)&a - (uintptr_t)&program_stack_start);
	if (distance < closestDistance) {
		closestDistance = distance;

		D_PRINTLN("%d bytes in stack %d free bytes in stack at %s",
		          (int32_t)((uintptr_t)&program_stack_end - (uintptr_t)&a), distance, caller);
		if (distance < 200) {
			FREEZE_WITH_ERROR("E338");
			D_PRINTLN("COLLISION");
		}
	}
#endif
}
