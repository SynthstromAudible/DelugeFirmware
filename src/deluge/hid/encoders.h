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

#pragma once

#include <cstddef>
#include <cstdint>

namespace deluge::hid::encoders {

// ── Encoder type hierarchy ────────────────────────────────────────────────

/// Base: converts raw A-pin edge counts (signed) into ticks.
/// Two A-pin edges = one quadrature cycle = one detent click.
class EncoderBase {
public:
	EncoderBase(const EncoderBase&) = delete;
	EncoderBase& operator=(const EncoderBase&) = delete;

protected:
	EncoderBase() = default;

	/// Converts edges → ticks (edges/2), accumulating the remainder.
	int8_t edgesToTicks(int8_t edges);

private:
	int8_t edgeAccumulator = 0;
};

/// Black function encoders (SCROLL_X/Y, TEMPO, SELECT): detented, dispatch clamped UI actions.
class DetentedEncoder : public EncoderBase {
public:
	DetentedEncoder() = default;

	void applyEdges(int8_t edges);

	/// True if any detent steps are waiting to be consumed.
	bool pending() const { return pos != 0; }

	/// Returns ±1 (clamped) and resets pos to 0.
	int32_t take();

	/// Puts a value back (used by the SD card-routine retry path).
	void restore(int32_t val) { pos = val; }

private:
	int8_t pos = 0;
};

/// Gold mod encoders (MOD_0, MOD_1): continuous, accumulate raw ticks for velocity.
class ContinuousEncoder : public EncoderBase {
public:
	ContinuousEncoder() = default;

	void applyEdges(int8_t edges);

	/// True if any ticks are waiting to be consumed.
	bool pending() const { return pos != 0; }

	/// Returns the accumulated tick count and resets pos to 0.
	int8_t take();

private:
	int8_t pos = 0;
};

// ── Named encoder globals ─────────────────────────────────────────────────

extern DetentedEncoder scrollY;
extern DetentedEncoder scrollX;
extern DetentedEncoder tempo;
extern DetentedEncoder select;
extern ContinuousEncoder mod0; ///< lower gold encoder
extern ContinuousEncoder mod1; ///< upper gold encoder

// ── Array-index access for iteration ─────────────────────────────────────

constexpr size_t kNumFunctionEncoders = 4;
constexpr size_t kNumModEncoders = 2;

DetentedEncoder& functionEncoderAt(size_t i); ///< 0=scrollY 1=scrollX 2=tempo 3=select
ContinuousEncoder& modEncoderAt(size_t i);    ///< 0=mod0 1=mod1

// ── Shared timestamp ──────────────────────────────────────────────────────

/// Last AudioEngine::audioSampleTimer tick at which we noticed a change on one of the mod encoders.
/// Defined in encoder_input.cpp; also written by view.cpp.
extern uint32_t timeModEncoderLastTurned[];

// ── Lifecycle ─────────────────────────────────────────────────────────────

void init();

} // namespace deluge::hid::encoders
