/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "RZA1/gpio/gpio.h"
#include "RZA1/system/r_typedefs.h"
#include "RZA1/uart/sio_char.h"
#include "definitions.h"
#include "deluge/deluge.h"
#include "deluge/drivers/mtu/mtu.h"
#include "scheduler_api.h"

static void midiAndGateOutputTimerInterrupt(uint32_t int_sense) {

	/* Stop the count of channel 2 of MTU2. */
	disableTimer(TIMER_MIDI_GATE_OUTPUT);

	/* Disable the MTU2 channel 2 interrupt */
	R_INTC_Disable(INTC_ID_TGIA[TIMER_MIDI_GATE_OUTPUT]);

	timerClearCompareMatchTGRA(TIMER_MIDI_GATE_OUTPUT);
	midiAndGateTimerGoneOff();
	// re enabled at the end of the audio routine iff the gate needs to be triggered between renders
}

uint32_t triggerClockRisingEdgeTimes[TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED];

uint32_t triggerClockRisingEdgesReceived = 0;
uint32_t triggerClockRisingEdgesProcessed = 0;

static void triggerClockInputHandler(uint32_t sense) {
	uint16_t dummy_read;

	R_INTC_Disable(IRQ_INTERRUPT_0 + 6);

	triggerClockRisingEdgeTimes[triggerClockRisingEdgesReceived & (TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED - 1)] =
	    DMACnNonVolatile(SSI_TX_DMA_CHANNEL).CRSA_n; // Reading this not as volatile works fine

	// uartPrintln("int");
	triggerClockRisingEdgesReceived++;

	clearIRQInterrupt(6);

	R_INTC_Enable(IRQ_INTERRUPT_0 + 6);
}

/******************************************************************************
 * Function Name: main
 * Description  : Displays the sample program information on the terminal
 *              : connected with the CPU board by the UART, and executes initial
 *              : setting for the PORT connected with the LEDs on the board.
 *              : Executes initial setting for the OSTM channel 0.
 * Arguments    : none
 * Return Value : 0
 ******************************************************************************/
int main(void) {

	// SSI pins
	setPinMux(7, 11, 6); // AUDIO_XOUT
	setPinMux(6, 9, 3);  // SSI0 word select
	setPinMux(6, 10, 3); // SSI0 tx
	setPinMux(6, 8, 3);  // SSI0 serial clock
	setPinMux(6, 11, 3); // SSI0 rx

	mtuEnableAccess();

	// Set up MIDI / gate output timer
	// this timer is setup but not enabled, as generally this will be processed by the audio routine.
	// Enabled in audio routine when the gate output will be during the next render window and the window cannot be
	// shortened to accommodate it. It's probably a waste of a system timer and can likely be refactored out
	setupTimerWithInterruptHandler(TIMER_MIDI_GATE_OUTPUT, 64, midiAndGateOutputTimerInterrupt, 5);

	// Original comment regarding above priority: "Must be greater than 9, so less prioritized than USB interrupt, so
	// that can still happen while this happening. But must be lower number / more prioritized than MIDI UART TX DMA
	// interrupt! Or else random crash occasionally." But, I've now undone the change in "USB sending as host now done
	// in ISR too!" commit, which set it to 11. That was causing the SD / UART lockups (checked and observed again
	// around V4.0.0-beta2), and was possibly only actually done in the first place to help with my hack fix for what I
	// thought was that USB "hardware bug", which I ended up resolving later anyway.

	// Set up slow system timer - 33 ticks per millisecond (30.30303 microseconds per tick) on A1
	setupRunningClock(TIMER_SYSTEM_SLOW, 1024);

	// Set up fast system timer - 528 ticks per millisecond (1.893939 microseconds per tick) on A1
	setupRunningClock(TIMER_SYSTEM_FAST, 64);

	// Set up super-fast system timer - 33.792 ticks per microsecond (29.5928 nanoseconds per tick) on A1
	setupRunningClock(TIMER_SYSTEM_SUPERFAST, 1);

	// Uart setup and pin mux ------------------------------------------------------------------------------------------

	// Uart for MIDI
	uartInit(UART_ITEM_MIDI, 31250);

	setPinMux(6, 15, 5); // TX
	setPinMux(6, 14, 5); // RX

	// Uart for PIC / display
	uartInit(UART_ITEM_PIC, UART_INITIAL_SPEED_PIC_PADS_HZ);

	setPinMux(3, 15, 5); // TX
	setPinMux(1, 9, 3);  // RX

	initUartDMA();

	// Pin mux for SD
	setPinMux(7, 0, 3); // CD
	setPinMux(7, 1, 3); // WP
	setPinMux(7, 2, 3); // D1
	setPinMux(7, 3, 3); // D0
	setPinMux(7, 4, 3); // CLK
	setPinMux(7, 5, 3); // CMD
	setPinMux(7, 6, 3); // D3
	setPinMux(7, 7, 3); // D2

	/* Configure IRQs detections on falling edge. Due to the presence of a transistor, we want to read falling edges on
	 * the trigger clock rather than rising. */
	INTC.ICR1 = 0b0101010101010101;
	// this is the same priority as the midi/gate interrupt despite the comment saying they need to be different
	setupAndEnableInterrupt(triggerClockInputHandler, IRQ_INTERRUPT_0 + 6, 5);
	ENABLE_INTERRUPTS();
	deluge_main();

	while (1) {
		;
	}
}

/* End of File */
