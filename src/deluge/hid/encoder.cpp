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

#include "hid/encoder.h"

extern "C" {
#include "RZA1/gpio/gpio.h"
}

namespace deluge::hid::encoders {

void encoderSetPins(uint8_t port, uint8_t pinA, uint8_t pinB) {
	setPinAsInput(port, pinA);
	setPinAsInput(port, pinB);
}

int8_t EncoderBase::edgesToTicks(int8_t edges) {
	if (edges == 0) {
		return 0;
	}
	// Two A-pin edges per quadrature cycle, one quadrature cycle per detent click on
	// the Deluge encoders. Same halving as the embassy `take_detents()` does.
	edgeAccumulator += edges;
	int8_t ticks = edgeAccumulator / 2;
	edgeAccumulator -= ticks * 2;
	return ticks;
}

void DetentedEncoder::applyEdges(int8_t edges) {
	int8_t ticks = edgesToTicks(edges);
	if (ticks != 0) {
		detentPos += ticks;
	}
}

int32_t DetentedEncoder::getLimitedDetentPosAndReset() {
	int32_t toReturn = (detentPos >= 0) ? 1 : -1;
	detentPos = 0;
	return toReturn;
}

void ContinuousEncoder::applyEdges(int8_t edges) {
	int8_t ticks = edgesToTicks(edges);
	if (ticks != 0) {
		encPos += ticks;
	}
}

} // namespace deluge::hid::encoders
