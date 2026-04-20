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
#include "RZA1/uart/sio_char.h"
#include "deluge/deluge.h"
#include "deluge/drivers/mtu/mtu.h"
#include "stdatomic.h"
#ifdef __cplusplus
extern "C" {
#endif

atomic_int critical_section_depth = 0;

void ENTER_CRITICAL_SECTION() {
	atomic_int expected = 0;
	atomic_int desired = 1;
	// if we go from 0 to 1 then we need to issue the barrier instructions, otherwise just increment depth
	if (atomic_compare_exchange_strong(&critical_section_depth, &expected, desired)) {
		// memory creates a memory barrier in GCC to avoid reordering
		// http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html#ss5.3
		__asm volatile("CPSID i" ::: "memory");
		__asm volatile("DSB");
		__asm volatile("ISB");
	}
	else {
		critical_section_depth++;
	}
}

void EXIT_CRITICAL_SECTION() {

	/// if the section depth is greater than 0 then we are guaranteed to be in a critical section and therefore
	/// can't have a toctou issue here
	if (critical_section_depth > 0) {
		atomic_fetch_sub(&critical_section_depth, 1);
		if (critical_section_depth == 0) {
			__asm volatile("CPSIE i" ::: "memory");
			__asm volatile("DSB");
			__asm volatile("ISB");
		}
	}
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
