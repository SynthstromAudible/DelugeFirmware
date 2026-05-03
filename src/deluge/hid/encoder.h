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
#include "atomic"

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
	int8_t readPos() { return encPos.exchange(0, std::memory_order_relaxed); }
	int8_t readDetentPos() { return detentPos.exchange(0, std::memory_order_relaxed); }
	int8_t getDetentPos() const { return detentPos.load(std::memory_order_relaxed); }
	/// just in case we have to backtrack because we can't actually action this yet, and this is somehow the least
	/// insane way to deal with it
	int8_t putDetentsBack(int8_t detents) { return detentPos.fetch_add(detents, std::memory_order_relaxed); }
	double currentKnobSpeed{0.0}; // Used for encoder acceleration

private:
	int8_t edgeAccumulator; // Leftover edges from applyEdges() that haven't yet formed a tick.
	bool doDetents;
	std::atomic_int8_t encPos;    // Keeps track of knob's position relative to centre of closest detent
	std::atomic_int8_t detentPos; // Number of full detents offset since functions last dealt with
};

} // namespace deluge::hid::encoders
