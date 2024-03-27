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
#ifdef __cplusplus
extern "C" {
#endif

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

#define DISABLE_INTERRUPTS_COUNT (sizeof(interruptsToDisable) / sizeof(uint32_t))
uint32_t interruptsToDisable[] = {INTC_ID_SPRI0,
                                  INTC_ID_DMAINT0 + PIC_TX_DMA_CHANNEL,
                                  IRQ_INTERRUPT_0 + 6,
                                  INTC_ID_USBI0,
                                  INTC_ID_SDHI1_0,
                                  INTC_ID_SDHI1_3,
                                  INTC_ID_DMAINT0 + OLED_SPI_DMA_CHANNEL,
                                  INTC_ID_DMAINT0 + MIDI_TX_DMA_CHANNEL,
                                  INTC_ID_SDHI1_1};
uint8_t enabledInterrupts[DISABLE_INTERRUPTS_COUNT] = {0};
void disableInterrupts() {

	for (uint32_t idx = 0; idx < DISABLE_INTERRUPTS_COUNT; ++idx) {
		enabledInterrupts[idx] = R_INTC_Enabled(interruptsToDisable[idx]);
		if (enabledInterrupts[idx]) {
			R_INTC_Disable(interruptsToDisable[idx]);
		}
	}
}
void reenableInterrupts() {
	for (uint32_t idx = 0; idx < DISABLE_INTERRUPTS_COUNT; ++idx) {
		if (enabledInterrupts[idx] != 0) {
			R_INTC_Enable(interruptsToDisable[idx]);
		}
	}
}
#ifdef __cplusplus
}
#endif
