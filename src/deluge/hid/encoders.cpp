/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoders.h"
#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "hid/encoder_quadrature.h"
#include <algorithm>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/intc/devdrv_intc.h"
}

namespace deluge::hid::encoders {

TaskID EncoderTaskID = -1;

double ContinuousEncoder::calcNextKnobSpeed(int8_t offset) {
	return acceleration_.advance(offset, getSystemTime(), kGoldEncoderAcceleration);
}

// ── Named encoder globals ──────────────────────────────────────────────────

DetentedEncoder scrollY;
DetentedEncoder scrollX;
DetentedEncoder tempo;
DetentedEncoder select;
ContinuousEncoder mod0;
ContinuousEncoder mod1;

DetentedEncoder& functionEncoderAt(size_t i) {
	static DetentedEncoder* const table[] = {&scrollY, &scrollX, &tempo, &select};
	return *table[i];
}

ContinuousEncoder& modEncoderAt(size_t i) {
	static ContinuousEncoder* const table[] = {&mod0, &mod1};
	return *table[i];
}

// ── IRQ infrastructure ────────────────────────────────────────────────────

namespace {

struct EncoderIrqEntry {
	/// @brief  A-side pin that's routed via PFC alt-2 to RZ/A1L IRQn.
	uint8_t irqPin;

	/// @brief Companion (B-side) pin, read as plain GPIO inside the ISR.
	uint8_t compPin;
	uint8_t irqNum;

	/// @brief Flip the direction sense when the A/B wiring is swapped.
	bool invert;
};

constexpr size_t kNumEncoders = 6;
QuadratureDecoder decoders[kNumEncoders];
constexpr EncoderIrqEntry kEncoderIrqMap[kNumEncoders] = {
    /* scrollY */ {.irqPin = 8, .compPin = 10, .irqNum = 0, .invert = false},
    /* scrollX */ {.irqPin = 11, .compPin = 12, .irqNum = 3, .invert = false},
    /* tempo   */ {.irqPin = 6, .compPin = 7, .irqNum = 2, .invert = true},
    /* select  */ {.irqPin = 3, .compPin = 2, .irqNum = 7, .invert = true},
    /* mod1    */ {.irqPin = 5, .compPin = 4, .irqNum = 1, .invert = false},
    /* mod0    */ {.irqPin = 0, .compPin = 15, .irqNum = 4, .invert = false},
};

void apply_encoder_edges(size_t index, int8_t movement) {
	if (movement == 0)
		return;
	switch (index) {
	case 0:
		scrollY.apply_edges(movement);
		break;
	case 1:
		scrollX.apply_edges(movement);
		break;
	case 2:
		tempo.apply_edges(movement);
		break;
	case 3:
		select.apply_edges(movement);
		break;
	case 4:
		mod1.apply_edges(movement);
		break;
	case 5:
		mod0.apply_edges(movement);
		break;
	}
}

template <size_t IDX>
void encoderIrqHandler(uint32_t /*sense*/) {
	constexpr EncoderIrqEntry m = kEncoderIrqMap[IDX];
	bool a = readInput(1, m.irqPin) != 0;
	bool b = readInput(1, m.compPin) != 0;

	apply_encoder_edges(IDX, decoders[IDX].observeAEdge(a, b, m.invert));

	// Wake the (otherwise self-blocked) encoder task so it actions this movement.
	unblockTask(EncoderTaskID);

	clearIRQInterrupt(m.irqNum);
}

using IrqHandler = void (*)(uint32_t);
constexpr IrqHandler kEncoderIrqHandlers[kNumEncoders] = {
    &encoderIrqHandler<0>, &encoderIrqHandler<1>, &encoderIrqHandler<2>,
    &encoderIrqHandler<3>, &encoderIrqHandler<4>, &encoderIrqHandler<5>,
};

constexpr uint8_t kEncoderIrqPriority = 14;

void encoderSetPins(uint8_t port, uint8_t pinA, uint8_t pinB) {
	setPinAsInput(port, pinA);
	setPinAsInput(port, pinB);
}

void initInterrupts() {
	for (size_t i = 0; i < kNumEncoders; i++) {
		const auto& m = kEncoderIrqMap[i];

		// Route the A-side pin to its IRQn input via PFC alt-function 2.
		setPinMux(1, m.irqPin, 2);
		enableInputBuffer(1, m.irqPin);

		setPinAsInput(1, m.compPin);
		decoders[i].initialize(readInput(1, m.irqPin) != 0, readInput(1, m.compPin) != 0);

		setIRQInterruptBothEdges(m.irqNum);

		clearIRQInterrupt(m.irqNum);
		setupAndEnableInterrupt(kEncoderIrqHandlers[i], INTC_ID_IRQ0 + m.irqNum, kEncoderIrqPriority);
	}
}

} // namespace

void init() {
	encoderSetPins(1, 11, 12); // scrollX
	encoderSetPins(1, 7, 6);   // tempo
	encoderSetPins(1, 0, 15);  // mod0
	encoderSetPins(1, 5, 4);   // mod1
	encoderSetPins(1, 8, 10);  // scrollY
	encoderSetPins(1, 2, 3);   // select

	initInterrupts();
}

void poll_encoder_pins() {
	CriticalSectionGuard guard;
	for (size_t i = 0; i < kNumEncoders; ++i) {
		const auto& m = kEncoderIrqMap[i];
		apply_encoder_edges(i,
		                    decoders[i].observe(readInput(1, m.irqPin) != 0, readInput(1, m.compPin) != 0, m.invert));
	}
}

} // namespace deluge::hid::encoders
