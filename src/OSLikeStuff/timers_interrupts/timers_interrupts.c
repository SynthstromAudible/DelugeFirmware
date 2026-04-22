/*
 * Copyright © 2014-2023 Mark Adams
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

#include "timers_interrupts.h"

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"
#include "deluge/drivers/mtu/mtu.h"
#include "stdatomic.h"
#ifdef __cplusplus
extern "C" {
#endif

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
		__asm volatile("CPSIE i" ::: "memory");
		__asm volatile("ISB");
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

void clearIRQInterrupt(int irqNumber) {
	uint16_t flagRead = INTC.IRQRR.WORD;
	if (flagRead & (1 << irqNumber)) {
		INTC.IRQRR.WORD = flagRead & ~(1 << irqNumber);
	}
}

void setupTimerWithInterruptHandler(int timerNo, int scale, void (*handler)(uint32_t intSense), uint8_t priority) {
	disableTimer(timerNo);
	*TCNT[timerNo] = 0u;
	timerClearCompareMatchTGRA(timerNo);
	timerEnableInterruptsTGRA(timerNo);
	timerControlSetup(timerNo, 1, scale);

	/* The setup process the interrupt IntTgfa function.*/
	R_INTC_RegistIntFunc(INTC_ID_TGIA[timerNo], handler);
	R_INTC_SetPriority(INTC_ID_TGIA[timerNo], priority);
}
void setupRunningClock(int timer, int preScale) {
	disableTimer(timer);
	timerControlSetup(timer, 0, preScale);
	enableTimer(timer);
}
void setupAndEnableInterrupt(void (*handler)(uint32_t), uint16_t interruptID, uint8_t priority) {
	R_INTC_Disable(interruptID);
	R_INTC_RegistIntFunc(interruptID, handler);
	R_INTC_SetPriority(interruptID, priority);
	R_INTC_Enable(interruptID);
}
#ifdef __cplusplus
}
#endif
