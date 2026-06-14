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

#include "OSLikeStuff/scheduler_api.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deluge::hid::encoders {

/// Black function encoders (SCROLL_X/Y, TEMPO, SELECT) are detented.
/// Two A-pin edges = one quadrature cycle = one detent click.
class DetentedEncoder {
public:
	DetentedEncoder() = default;
	DetentedEncoder(const DetentedEncoder&) = delete;
	DetentedEncoder& operator=(const DetentedEncoder&) = delete;

	void applyEdges(int8_t edges);

	/// True if any detent steps are waiting to be consumed.
	bool pending() const { return pos.load(std::memory_order_relaxed) != 0; }

	/// Returns the accumulated signed detent count and resets pos to 0.
	int32_t take();

	/// Puts a value back (used by the SD card-routine retry path).
	/// fetch_add rather than store so a detent that arrived from the IRQ since take() isn't clobbered.
	void restore(int32_t val) { pos.fetch_add(val, std::memory_order_relaxed); }

private:
	int32_t edgeAccumulator = 0; ///< Accumulates A-pin edges until we have a full detent click. IRQ-only.
	std::atomic_int32_t pos = 0; ///< Written by the IRQ (applyEdges), drained by the encoder task.
};

/// Gold mod encoders (MOD_0, MOD_1) are continuous, and accumulate raw edges for velocity.
class ContinuousEncoder {
public:
	ContinuousEncoder() = default;
	ContinuousEncoder(const ContinuousEncoder&) = delete;
	ContinuousEncoder& operator=(const ContinuousEncoder&) = delete;

	void applyEdges(int8_t edges) { pos.fetch_add(edges, std::memory_order_relaxed); }

	/// True if any ticks are waiting to be consumed.
	bool pending() const { return pos.load(std::memory_order_relaxed) != 0; }

	/// Returns the accumulated tick count and resets pos to 0.
	int8_t take();

	/// Returns multiplier for encoder offset
	double calcNextKnobSpeed(int8_t offset);

private:
	std::atomic_int8_t pos = 0;   ///< Written by the IRQ (applyEdges), drained by the encoder task.
	double currentKnobSpeed{0.0}; // Used for encoder acceleration
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

/// Scheduler task that drains the encoders. It blocks itself after each run and is unblocked from the
/// encoder IRQ, so it only consumes CPU when an encoder has actually moved. Set by registerTasks().
extern TaskID EncoderTaskID;

} // namespace deluge::hid::encoders
