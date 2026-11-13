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

#include "hid/encoder_acceleration.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace deluge::hid::encoders {

/// Black function encoders (SCROLL_X/Y, TEMPO, SELECT) are detented.
/// Four quadrature edges = one cycle = one detent click. The board confirms each edge one edge
/// late, so the first click needs three confirmed edges, continuing needs four, and a reversal
/// needs two (the pending edge is retraced before reverse edges confirm). Partial reverse travel
/// cancels on returning to the detent, without undoing the reported click or discarding forward
/// progress.
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

private:
	int32_t positiveThreshold() const { return lastDirection == 0 ? 3 : (lastDirection == 1 ? 4 : 2); }
	int32_t negativeThreshold() const { return lastDirection == 0 ? 3 : (lastDirection == -1 ? 4 : 2); }

	int8_t lastDirection = 0;    ///< Direction of the last reported click, not the last electrical edge.
	int32_t edgeAccumulator = 0; ///< Quadrature edges not yet a whole detent click.
	std::atomic_int32_t pos = 0; ///< Written by applyEdges, drained by the encoder task.
};

/// Gold mod encoders (MOD_0, MOD_1) are continuous, and accumulate raw edges for velocity: four per
/// quadrature cycle, as in 1.x.
///
/// The edges are kept as the runs the board decoded them in, each with its own time, because the acceleration
/// grows with how fast they came. `main`'s encoder task takes each run as its interrupt arrives
/// (`origin/main:src/deluge/hid/encoders.cpp`'s `encoderIrqHandler`); here the app takes them later, in batches,
/// and `actionTurns` replays each run on its own time, so the result does not depend on when the app ran.
class ContinuousEncoder {
public:
	/// The runs kept between two `actionTurns`: the board's per-take maximum (`DELUGE_ENCODER_EDGE_RUNS`).
	static constexpr size_t kMaxRuns = 17;

	ContinuousEncoder() = default;
	ContinuousEncoder(const ContinuousEncoder&) = delete;
	ContinuousEncoder& operator=(const ContinuousEncoder&) = delete;

	/// @brief Queue a run of \p edges the board decoded at \p time.
	/// @param time `getSystemTime()`'s clock, in seconds. When the queue is full the run folds into the newest one.
	void applyEdges(int8_t edges, double time) {
		if (edges == 0) {
			return;
		}
		if (count_ < kMaxRuns) {
			runs_[count_++] = Run{edges, time};
			return;
		}
		Run& newest = runs_[kMaxRuns - 1];
		newest.edges = static_cast<int8_t>(std::clamp(newest.edges + edges, -128, 127));
		newest.time = time;
	}

	/// @brief Queue a run of \p edges decoded now (`getSystemTime()`).
	void applyEdges(int8_t edges);

	/// True if any runs are waiting to be actioned.
	[[nodiscard]] bool pending() const { return count_ != 0; }

	/// @brief Hand each queued run to \p action as `main`'s `interpretEncoders` hands each turn it takes, oldest
	///        first, and empty the queue.
	///
	/// Every run goes out, as its edges times the gold acceleration at the run's own time, truncated to `int8_t` as
	/// `main`'s `offset_accelerated` is. `main`'s wiggle test writes no turn off: it compares the turn against
	/// `modEncoderInitialTurnDirection`, which nothing sets to anything but 0
	/// (`origin/main:src/deluge/hid/encoder_input.cpp`).
	/// @param action Called with each actioned turn's accelerated offset.
	/// @return Whether any run was queued.
	bool actionTurns(std::invocable<int8_t> auto&& action) {
		const size_t count = count_;
		count_ = 0;
		for (size_t i = 0; i < count; i++) {
			const Run run = runs_[i];
			action(
			    static_cast<int8_t>(run.edges * acceleration_.advance(run.edges, run.time, kGoldEncoderAcceleration)));
		}
		return count != 0;
	}

private:
	/// One run of edges and when the board decoded it.
	struct Run {
		int8_t edges = 0;
		double time = 0.0; ///< `getSystemTime()`'s clock, in seconds.
	};

	std::array<Run, kMaxRuns> runs_{};
	size_t count_ = 0;
	EncoderAcceleration acceleration_;
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

} // namespace deluge::hid::encoders
