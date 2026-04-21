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

#pragma once

#define encMinBacktrackTime (20 * 44) // In milliseconds/44
#include <cstdint>

namespace deluge::hid::encoders {

class Encoder {
public:
	Encoder();

	Encoder(Encoder& other) = delete;
	Encoder(Encoder&& other) = delete;
	Encoder& operator=(Encoder& other) = delete;
	Encoder&& operator=(Encoder&& other) = delete;

	/// Fold a number of A-pin edges (signed; from the IRQ accumulator) into encPos / detentPos.
	/// Two edges = one detent step (or one raw tick for non-detent gold knobs), matching the
	/// "1 quadrature cycle per detent" wiring on the Deluge encoders.
	void applyEdges(int8_t edges);
	void setPins(uint8_t pinA1New, uint8_t pinA2New, uint8_t pinB1New, uint8_t pinB2New);
	void setNonDetentMode();
	int32_t getLimitedDetentPosAndReset();
	int8_t encPos;    // Keeps track of knob's position relative to centre of closest detent
	int8_t detentPos; // Number of full detents offset since functions last dealt with
private:
	int8_t edgeAccumulator; // Leftover edges from applyEdges() that haven't yet formed a tick.
	bool doDetents;
};

} // namespace deluge::hid::encoders
