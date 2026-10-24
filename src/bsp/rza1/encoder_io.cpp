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
/// on both edges; the B-side (companion) pin is read as plain GPIO. A quadrature
/// decoder per encoder (deluge/hid/encoder_quadrature.h) turns the A-edge ISR
/// samples, plus the regular polls from deluge_encoder_poll_pins() that catch
/// slow B-only transitions, into confirmed signed edges. Those accumulate per
/// encoder; the ISR unblocks the application's encoder task, and the app drains
/// the counts via deluge_encoder_take_edges() and applies its own detent policy.

#include "libdeluge/encoder_io.h"
#include "libdeluge/system.h" // CriticalSectionGuard

#include "OSLikeStuff/scheduler_api.h"     // unblockTask
#include "deluge/hid/encoder_quadrature.h" // QuadratureDecoder (header-only, no app dependencies)

#include <atomic>

extern "C" {
#include "RZA1/gpio/gpio.h"        // setPinAsInput, setPinMux, enableInputBuffer, readInput
#include "RZA1/intc/devdrv_intc.h" // INTC_ID_IRQ0
#include "timers_interrupts.h"     // setupAndEnableInterrupt, set/clearIRQInterrupt (sibling BSP file)
}

using deluge::hid::encoders::QuadratureDecoder;

namespace {

/// Per-encoder pin/IRQ map (board layout). Indexed by the application's encoder
/// ordinal (EncoderName): SCROLL_Y, SCROLL_X, TEMPO, SELECT, MOD_1, MOD_0.
struct EncoderIrqEntry {
	uint8_t irqPin;  ///< A-side pin, routed to IRQn via PFC alt-2.
	uint8_t compPin; ///< B-side companion pin, read as plain GPIO.
	uint8_t irqNum;  ///< RZ/A1L IRQ number (INTC_ID_IRQ0 + irqNum).
	bool invert;     ///< flip direction sense where the A/B wiring is swapped.
};

constexpr EncoderIrqEntry kEncoderIrqMap[DELUGE_NUM_ENCODERS] = {
    {.irqPin = 8, .compPin = 10, .irqNum = 0, .invert = false},  // SCROLL_Y
    {.irqPin = 11, .compPin = 12, .irqNum = 3, .invert = false}, // SCROLL_X
    {.irqPin = 6, .compPin = 7, .irqNum = 2, .invert = true},    // TEMPO
    {.irqPin = 3, .compPin = 2, .irqNum = 7, .invert = true},    // SELECT
    {.irqPin = 5, .compPin = 4, .irqNum = 1, .invert = false},   // MOD_1
    {.irqPin = 0, .compPin = 15, .irqNum = 4, .invert = false},  // MOD_0
};

constexpr uint8_t kEncoderIrqPriority = 14;

QuadratureDecoder decoders[DELUGE_NUM_ENCODERS];

/// Signed confirmed quadrature edges, accumulated by the ISRs and polls, drained by the app task.
std::atomic<int8_t> encoderEdges[DELUGE_NUM_ENCODERS];

/// Scheduler task to wake when an encoder moves (set in deluge_encoder_io_init).
TaskID wakeTask = -1;

bool readA(const EncoderIrqEntry& m) {
	return readInput(1, m.irqPin) != 0;
}
bool readB(const EncoderIrqEntry& m) {
	return readInput(1, m.compPin) != 0;
}

void addEdges(unsigned idx, int8_t movement) {
	if (movement != 0) {
		encoderEdges[idx].fetch_add(movement, std::memory_order_relaxed);
	}
}

template <unsigned IDX>
void encoderIrqHandler(uint32_t /*sense*/) {
	constexpr EncoderIrqEntry m = kEncoderIrqMap[IDX];
	addEdges(IDX, decoders[IDX].observeAEdge(readA(m), readB(m), m.invert));
	// Wake the encoder task so it actions this movement promptly.
	unblockTask(wakeTask);
	clearIRQInterrupt(m.irqNum);
}

using EncoderIrqHandler = void (*)(uint32_t);
constexpr EncoderIrqHandler kEncoderIrqHandlers[DELUGE_NUM_ENCODERS] = {
    &encoderIrqHandler<0>, &encoderIrqHandler<1>, &encoderIrqHandler<2>,
    &encoderIrqHandler<3>, &encoderIrqHandler<4>, &encoderIrqHandler<5>,
};

} // namespace

extern "C" {

void deluge_encoder_io_init(int8_t wake_task) {
	wakeTask = wake_task;

	for (unsigned i = 0; i < DELUGE_NUM_ENCODERS; i++) {
		const EncoderIrqEntry& m = kEncoderIrqMap[i];

		// Route the A-side pin to its IRQn input via PFC alt-function 2.
		setPinMux(1, m.irqPin, 2);
		enableInputBuffer(1, m.irqPin);

		setPinAsInput(1, m.compPin);
		decoders[i].initialize(readA(m), readB(m));

		setIRQInterruptBothEdges(m.irqNum);
		clearIRQInterrupt(m.irqNum);
		setupAndEnableInterrupt(kEncoderIrqHandlers[i], INTC_ID_IRQ0 + m.irqNum, kEncoderIrqPriority);
	}
}

void deluge_encoder_poll_pins(void) {
	CriticalSectionGuard guard;
	for (unsigned i = 0; i < DELUGE_NUM_ENCODERS; i++) {
		const EncoderIrqEntry& m = kEncoderIrqMap[i];
		addEdges(i, decoders[i].observe(readA(m), readB(m), m.invert));
	}
}

int8_t deluge_encoder_take_edges(uint8_t encoder) {
	return encoderEdges[encoder].exchange(0, std::memory_order_relaxed);
}

} // extern "C"
