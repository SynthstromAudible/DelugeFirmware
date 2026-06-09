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

/// RZ/A1L implementation of <libdeluge/system.h>.
///
/// Part of bsp-rza1-legacy. Only the timer-quiesce path is implemented so far;
/// deluge_platform_init/reset/log are added as the boot wiring in main.c moves
/// behind the boundary.

#include "libdeluge/system.h"
#include "RZA1/cpu_specific.h"

#include "RZA1/mtu/mtu.h"
#include "definitions.h"
#include "stdatomic.h"

void deluge_system_quiesce(void) {
	// Disable all MTU system timers, as the chainload teardown previously did
	// inline. Behaviour is unchanged; only the layer moved.
	disableTimer(TIMER_MIDI_GATE_OUTPUT);
	disableTimer(TIMER_SYSTEM_SLOW);
	disableTimer(TIMER_SYSTEM_FAST);
	disableTimer(TIMER_SYSTEM_SUPERFAST);
}

// Critical sections: a CPU primitive (IRQ masking) the BSP owns. Relocated
// verbatim from OSLikeStuff/timers_interrupts — same shared depth counter and
// barrier sequence; only the layer moved. The depth lets nested enters from
// app -> driver avoid prematurely re-enabling interrupts on this single-core
// cooperative model.
_Atomic int32_t critical_section_depth = 0;

void ENTER_CRITICAL_SECTION() {
	int32_t expected = 0;
	int32_t desired = 1;
	// if we go from 0 to 1 then we need to issue the synchronization instructions, otherwise just increment depth
	// avoids having to clear the full pipeline
	if (atomic_compare_exchange_strong_explicit(&critical_section_depth, &expected, desired, memory_order_relaxed,
	                                            memory_order_relaxed)) {
		// memory creates a memory barrier in GCC to avoid reordering
		// http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html#ss5.3
		__asm volatile("CPSID i" ::: "memory");
		__asm volatile("DSB");
		__asm volatile("ISB");
	}
	// in this case we're already in a critical section so just increment the count
	else {
		atomic_fetch_add_explicit(&critical_section_depth, 1, memory_order_relaxed);
	}
}

void EXIT_CRITICAL_SECTION() {

	int32_t expected = 1;
	int32_t desired = 0;
	// if we go from 1 to 0 then exit the critical section. In any other case just decrement the counter
	if (atomic_compare_exchange_strong_explicit(&critical_section_depth, &expected, desired, memory_order_relaxed,
	                                            memory_order_relaxed)) {
		__asm volatile("DSB");
		__asm volatile("ISB");
		__asm volatile("CPSIE i" ::: "memory");
	}
	// if that fails we're still in a critical section so just decrement the count
	else {
		atomic_fetch_sub_explicit(&critical_section_depth, 1, memory_order_relaxed);
	}

#if ALPHA_OR_BETA_VERSTION
	if (atomic_load_explicit(&critical_section_depth, memory_order_relaxed) < 0) {
		FREEZE_WITH_ERROR("OH NO");
	}
#endif
}
