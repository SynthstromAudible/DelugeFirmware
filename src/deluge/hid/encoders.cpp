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
#include <atomic>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/intc/devdrv_intc.h"
}

namespace deluge::hid::encoders {

constexpr size_t kNumFunctionEncoders = util::to_underlying(EncoderName::MAX_FUNCTION_ENCODERS);
constexpr size_t kNumModEncoders = 2;

std::array<DetentedEncoder, kNumFunctionEncoders> functionEncoders = {};
std::array<ContinuousEncoder, kNumModEncoders> modEncoders = {};

namespace {
constexpr size_t kNumEncoders = util::to_underlying(EncoderName::MAX_ENCODER);
struct EncoderIrqEntry {
	/// @brief  A-side pin that's routed via PFC alt-2 to RZ/A1L IRQn.
	uint8_t irqPin;

	/// @brief companion (B-side) pin, read as plain GPIO inside the ISR.
	uint8_t compPin;
	uint8_t irqNum;

	/// @brief flip the direction sense when the A/B wiring is swapped relative to the polled order in `setPins(...)`.
	bool invert;
};

constexpr EncoderIrqEntry kEncoderIrqMap[kNumEncoders] = {
    [util::to_underlying(EncoderName::SCROLL_Y)] = {.irqPin = 8, .compPin = 10, .irqNum = 0, .invert = false},
    [util::to_underlying(EncoderName::SCROLL_X)] = {.irqPin = 11, .compPin = 12, .irqNum = 3, .invert = false},
    [util::to_underlying(EncoderName::TEMPO)] = {.irqPin = 6, .compPin = 7, .irqNum = 2, .invert = true},
    [util::to_underlying(EncoderName::SELECT)] = {.irqPin = 3, .compPin = 2, .irqNum = 7, .invert = true},
    [util::to_underlying(EncoderName::MOD_1)] = {.irqPin = 5, .compPin = 4, .irqNum = 1, .invert = false},
    [util::to_underlying(EncoderName::MOD_0)] = {.irqPin = 0, .compPin = 15, .irqNum = 4, .invert = false},
};

/// Atomic edge counters written by ISRs, drained by `readEncoders()`.
std::atomic<int8_t> encoderEdgeDeltas[kNumEncoders] = {};

template <size_t IDX>
void encoderIrqHandler(uint32_t /*sense*/) {
	constexpr EncoderIrqEntry m = kEncoderIrqMap[IDX];
	bool a = readInput(1, m.irqPin) != 0;
	bool b = readInput(1, m.compPin) != 0;
	bool cw = m.invert ? (a != b) : (a == b);
	int8_t inc = cw ? +1 : -1;
	encoderEdgeDeltas[IDX].fetch_add(inc, std::memory_order_relaxed);
	clearIRQInterrupt(m.irqNum);
}

using IrqHandler = void (*)(uint32_t);
constexpr IrqHandler kEncoderIrqHandlers[kNumEncoders] = {
    &encoderIrqHandler<0>, &encoderIrqHandler<1>, &encoderIrqHandler<2>,
    &encoderIrqHandler<3>, &encoderIrqHandler<4>, &encoderIrqHandler<5>,
};

constexpr uint8_t kEncoderIrqPriority = 14;

void initInterrupts() {
	for (size_t i = 0; i < kNumEncoders; i++) {
		const auto& m = kEncoderIrqMap[i];

		// Route the A-side pin to its IRQn input via PFC alt-function 2.
		setPinMux(1, m.irqPin, 2);
		enableInputBuffer(1, m.irqPin);

		setPinAsInput(1, m.compPin);

		setIRQInterruptBothEdges(m.irqNum);

		clearIRQInterrupt(m.irqNum);
		setupAndEnableInterrupt(kEncoderIrqHandlers[i], INTC_ID_IRQ0 + m.irqNum, kEncoderIrqPriority);
	}
}
} // namespace

DetentedEncoder& getFunctionEncoder(EncoderName which) {
	return functionEncoders[util::to_underlying(which)];
}

ContinuousEncoder& getModEncoder(int index) {
	return modEncoders[index];
}

void init() {
	// Set up pin directions for all encoders (interrupt routing handled in initInterrupts).
	encoderSetPins(1, 11, 12); // SCROLL_X
	encoderSetPins(1, 7, 6);   // TEMPO
	encoderSetPins(1, 0, 15);  // MOD_0
	encoderSetPins(1, 5, 4);   // MOD_1
	encoderSetPins(1, 8, 10);  // SCROLL_Y
	encoderSetPins(1, 2, 3);   // SELECT

	initInterrupts();
}

void readEncoders() {
	for (size_t i = 0; i < kNumFunctionEncoders; i++) {
		int8_t edges = encoderEdgeDeltas[i].exchange(0, std::memory_order_relaxed);
		if (edges != 0) {
			functionEncoders[i].applyEdges(edges);
		}
	}
	for (size_t i = 0; i < kNumModEncoders; i++) {
		// modEncoders[0]=MOD_0 (delta index 5), modEncoders[1]=MOD_1 (delta index 4)
		int8_t edges =
		    encoderEdgeDeltas[util::to_underlying(EncoderName::MOD_0) - i].exchange(0, std::memory_order_relaxed);
		if (edges != 0) {
			modEncoders[i].applyEdges(edges);
		}
	}
}

} // namespace deluge::hid::encoders
