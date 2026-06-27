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

/// RZ/A1L implementation of <libdeluge/encoder_io.h>.
///
/// Each encoder's A-side pin is routed (PFC alt-2) to an RZ/A1L IRQn that fires
/// on both edges; the B-side (companion) pin is read as plain GPIO inside the
/// ISR to resolve direction. The ISR accumulates a signed edge count per encoder
/// and unblocks the application's encoder task; the app drains the counts via
/// deluge_encoder_take_edges() and applies its own detent policy.

#include "libdeluge/encoder_io.h"

#include "OSLikeStuff/scheduler_api.h" // unblockTask
#include "RZA1/gpio/gpio.h"            // setPinAsInput, setPinMux, enableInputBuffer, readInput
#include "RZA1/intc/devdrv_intc.h"     // INTC_ID_IRQ0
#include "timers_interrupts.h"         // setupAndEnableInterrupt, set/clearIRQInterrupt (sibling BSP file)

#include <stdatomic.h>

/// Per-encoder pin/IRQ map (board layout). Indexed by the application's encoder
/// ordinal (EncoderName): SCROLL_Y, SCROLL_X, TEMPO, SELECT, MOD_1, MOD_0.
typedef struct {
	uint8_t irqPin;  ///< A-side pin, routed to IRQn via PFC alt-2.
	uint8_t compPin; ///< B-side companion pin, read as plain GPIO in the ISR.
	uint8_t irqNum;  ///< RZ/A1L IRQ number (INTC_ID_IRQ0 + irqNum).
	bool invert;     ///< flip direction sense where the A/B wiring is swapped.
} EncoderIrqEntry;

static const EncoderIrqEntry kEncoderIrqMap[DELUGE_NUM_ENCODERS] = {
    {.irqPin = 8, .compPin = 10, .irqNum = 0, .invert = false},  // SCROLL_Y
    {.irqPin = 11, .compPin = 12, .irqNum = 3, .invert = false}, // SCROLL_X
    {.irqPin = 6, .compPin = 7, .irqNum = 2, .invert = true},    // TEMPO
    {.irqPin = 3, .compPin = 2, .irqNum = 7, .invert = true},    // SELECT
    {.irqPin = 5, .compPin = 4, .irqNum = 1, .invert = false},   // MOD_1
    {.irqPin = 0, .compPin = 15, .irqNum = 4, .invert = false},  // MOD_0
};

static const uint8_t kEncoderIrqPriority = 14;

/// Signed A-pin edges accumulated by the ISRs, drained by the app task.
static _Atomic int8_t encoderEdges[DELUGE_NUM_ENCODERS];

/// Scheduler task to wake when an encoder moves (set in deluge_encoder_io_init).
static TaskID wakeTask = -1;

static void handleEncoderEdge(unsigned idx) {
	const EncoderIrqEntry m = kEncoderIrqMap[idx];
	bool a = readInput(1, m.irqPin) != 0;
	bool b = readInput(1, m.compPin) != 0;
	bool cw = m.invert ? (a != b) : (a == b);
	atomic_fetch_add_explicit(&encoderEdges[idx], cw ? +1 : -1, memory_order_relaxed);
	unblockTask(wakeTask);
	clearIRQInterrupt(m.irqNum);
}

// One ISR per encoder (setupAndEnableInterrupt takes a plain function pointer).
#define ENCODER_IRQ_HANDLER(IDX)                                                                                       \
	static void encoderIrqHandler##IDX(uint32_t sense) {                                                               \
		(void)sense;                                                                                                   \
		handleEncoderEdge(IDX);                                                                                        \
	}
ENCODER_IRQ_HANDLER(0)
ENCODER_IRQ_HANDLER(1)
ENCODER_IRQ_HANDLER(2)
ENCODER_IRQ_HANDLER(3)
ENCODER_IRQ_HANDLER(4)
ENCODER_IRQ_HANDLER(5)
#undef ENCODER_IRQ_HANDLER

typedef void (*EncoderIrqHandler)(uint32_t);
static const EncoderIrqHandler kEncoderIrqHandlers[DELUGE_NUM_ENCODERS] = {
    &encoderIrqHandler0, &encoderIrqHandler1, &encoderIrqHandler2,
    &encoderIrqHandler3, &encoderIrqHandler4, &encoderIrqHandler5,
};

void deluge_encoder_io_init(int8_t wake_task) {
	wakeTask = wake_task;

	for (unsigned i = 0; i < DELUGE_NUM_ENCODERS; i++) {
		const EncoderIrqEntry m = kEncoderIrqMap[i];

		// Route the A-side pin to its IRQn input via PFC alt-function 2.
		setPinMux(1, m.irqPin, 2);
		enableInputBuffer(1, m.irqPin);

		setPinAsInput(1, m.compPin);

		setIRQInterruptBothEdges(m.irqNum);
		clearIRQInterrupt(m.irqNum);
		setupAndEnableInterrupt(kEncoderIrqHandlers[i], INTC_ID_IRQ0 + m.irqNum, kEncoderIrqPriority);
	}
}

int8_t deluge_encoder_take_edges(uint8_t encoder) {
	return atomic_exchange_explicit(&encoderEdges[encoder], 0, memory_order_relaxed);
}
