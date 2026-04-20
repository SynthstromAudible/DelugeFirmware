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

#include <cstdint>

namespace deluge::hid::encoders {

/// Call setPinAsInput for both pins of an encoder on init.
void encoderSetPins(uint8_t port, uint8_t pinA, uint8_t pinB);

/// Base: converts raw A-pin edge counts (signed) into ticks.
/// Two A-pin edges = one quadrature cycle = one detent click on Deluge encoders.
class EncoderBase {
public:
	EncoderBase(const EncoderBase&) = delete;
	EncoderBase& operator=(const EncoderBase&) = delete;

protected:
	EncoderBase() = default;

	/// Converts edges → ticks (edges/2), accumulating the remainder.
	/// Returns the number of whole ticks produced (may be 0).
	int8_t edgesToTicks(int8_t edges);

private:
	int8_t edgeAccumulator = 0;
};

/// Black function encoders (SCROLL_X/Y, TEMPO, SELECT): detented, dispatch clamped UI actions.
class DetentedEncoder : public EncoderBase {
public:
	DetentedEncoder() = default;

	/// Fold edges into detentPos.
	void applyEdges(int8_t edges);

	/// Returns ±1 and resets detentPos to 0.
	int32_t getLimitedDetentPosAndReset();

	/// Number of full detent steps accumulated since the last consumer read.
	int8_t detentPos = 0;
};

/// Gold mod encoders (MOD_0, MOD_1): continuous, accumulate raw ticks for velocity.
class ContinuousEncoder : public EncoderBase {
public:
	ContinuousEncoder() = default;

	/// Fold edges into encPos.
	void applyEdges(int8_t edges);

	/// Raw tick count accumulated since the last consumer read.
	int8_t encPos = 0;
};

} // namespace deluge::hid::encoders
