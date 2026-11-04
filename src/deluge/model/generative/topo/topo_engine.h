/*
 * Copyright © 2026 Synthstrom Audible Limited
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
 *
 * -----------------------------------------------------------------------------
 *
 * "Topo Drums" generative drum-pattern engine.
 *
 * Ported from the pattern generator of the original Mutable Instruments Grids
 * firmware: Copyright 2011/2012 Emilie Gillet (emilie.o.gillet@gmail.com),
 * GPLv3. "Mutable Instruments" and "Grids" are trademarks of Mutable Instruments
 * and are not used as the name of this feature.
 *
 * The engine morphs between 25 reference drum patterns (a "topographic map"),
 * with per-bank density thresholds, per-loop randomness and accents; plus a
 * Euclidean mode. See topo_pattern_data.h for the pattern tables.
 *
 * Deluge-specific changes vs. the original: instance-based state (no globals),
 * no EEPROM persistence, the hardware output byte is replaced by explicit
 * fire/accent masks, and tempo/clock-source/options handling is dropped (the
 * Deluge transport drives the engine via advance()). Everything numeric
 * (interpolation, thresholds, RNG sequence) is bit-exact with the original.
 */

#pragma once

#include "model/generative/topo/topo_pattern_data.h"
#include <cstdint>

namespace deluge::model::topo {

namespace detail {

/// Fixed-point helpers reproducing avrlib's AVR-ASM semantics exactly.

/// (b * balance + a * (255 - balance)) >> 8
inline uint8_t u8Mix(uint8_t a, uint8_t b, uint8_t balance) {
	return (static_cast<uint16_t>(b) * balance + static_cast<uint16_t>(a) * (255 - balance)) >> 8;
}

/// (a * b) >> 8
inline uint8_t u8u8MulShift8(uint8_t a, uint8_t b) {
	return static_cast<uint16_t>(a) * b >> 8;
}

} // namespace detail

/// Fast 16-bit pseudo random number generator - faithful port of avrlib's Random class.
/// Galois LFSR with feedback polynomial x^16 + x^14 + x^13 + x^11, period 65535.
class TopoRng {
public:
	explicit TopoRng(uint16_t seed = kDefaultSeed) : state_(seed) {}

	void seed(uint16_t newSeed) {
		// Divergence from original: avoid the degenerate all-zero state.
		state_ = newSeed == 0 ? kDefaultSeed : newSeed;
	}
	void update() { state_ = (state_ >> 1) ^ (static_cast<uint16_t>(-(state_ & 1)) & 0xB400); }
	uint16_t state() const { return state_; }
	uint8_t stateMsb() const { return state_ >> 8; }
	uint8_t getByte() {
		update();
		return stateMsb();
	}

	static constexpr uint16_t kDefaultSeed = 0x21; // as in avrlib

private:
	uint16_t state_;
};

enum class TopoMode : uint8_t {
	Euclidean = 0,
	Drums = 1,
};

struct TopoSettings {
	/// Topographic mode: map position / Euclidean mode: bank pattern lengths (1-32 via >>3).
	uint8_t x{128};
	uint8_t y{128};
	/// Topographic mode: per-loop level perturbation. With swing enabled this is
	/// reinterpreted as the swing depth (perturbation forced to 0).
	/// Euclidean mode: bank C pattern length.
	uint8_t randomness{0};
	/// Topographic mode: level threshold per bank (CW = busier).
	/// Euclidean mode: note count per bank (0-31 via >>3).
	uint8_t density[kNumBanks]{128, 128, 128};
	/// Level above which a hit is flagged as an accent. Original hardware used a
	/// fixed 192; exposed so the UI can offer it as a parameter.
	uint8_t accentThreshold{192};

	bool operator==(TopoSettings const&) const = default;
};

/// Result of one advance() call: everything that happened to the pattern while the
/// transport clock moved forward.
struct TopoStepEvent {
	/// How many step boundaries were crossed and evaluated (0 if no new step).
	uint8_t stepsCrossed{0};
	/// True if step 0 was among the evaluated steps (loop restart; perturbations re-rolled).
	bool loopWrapped{false};
	/// Index (0-31) of the last evaluated step.
	uint8_t step{0};
	/// Bitmask of banks that fired on the evaluated step(s).
	uint8_t fireMask{0};
	/// Bitmask of banks that were accented (Drums mode) or started a Euclidean cycle
	/// (Euclidean mode) on the evaluated step(s).
	uint8_t accentMask{0};

	bool fired(uint8_t bank) const { return fireMask & (1 << bank); }
	bool accented(uint8_t bank) const { return accentMask & (1 << bank); }
};

/// Generative drum-pattern engine. Clock unit is the "pulse": there are
/// kPulsesPerStep (3) pulses per step and kPulsesPerPattern (96) pulses per bar,
/// i.e. 24 PPQN - the same granularity the original Grids hardware used.
class TopoEngine {
public:
	TopoEngine() { reset(); }

	/// Jump back to step 0 (like pressing the original's RESET button). Perturbations
	/// are re-rolled at the next step-0 evaluation.
	void reset() {
		step_ = 0;
		pulse_ = 0;
		for (uint8_t& s : euclideanStep_) {
			s = 0;
		}
	}

	/// Advance the pattern clock by numPulses (24 PPQN pulses). Each step boundary
	/// crossed re-evaluates the pattern for that step. If several boundaries are
	/// crossed in one call (e.g. transport catch-up), fireMask/accentMask are the
	/// union over all crossed steps so no hit is dropped.
	TopoStepEvent advance(uint32_t numPulses);

	uint8_t step() const { return step_; }
	uint8_t pulse() const { return pulse_; }

	TopoMode mode() const { return mode_; }
	void setMode(TopoMode mode) { mode_ = mode; }
	bool swing() const { return swing_; }
	void setSwing(bool swing) { swing_ = swing; }
	/// Trigger vs gate output behaviour. The engine itself only reports per-step
	/// events; the caller (kit integration) holds gates for the whole step when set.
	bool gateMode() const { return gateMode_; }
	void setGateMode(bool gateMode) { gateMode_ = gateMode; }

	TopoSettings& settings() { return settings_; }
	TopoSettings const& settings() const { return settings_; }

	/// Swing offset for the current step, 0 when swing is off or in Euclidean mode.
	/// Range 0..42 in units of the original's main clock ticks - the caller maps this
	/// to transport ticks (Grids applied it by shifting the clock's wrap threshold,
	/// delaying every second pair of steps).
	int8_t swingAmount() const;

	/// Random bit refreshed once per pulse (the original's noise/S&H output bit).
	bool noiseBit() const { return noiseBit_; }

	/// Re-seed the internal RNG (tests / reproducible behaviour). 0 selects the default seed.
	void seedRng(uint16_t seed) { rng_.seed(seed); }
	uint16_t rngState() const { return rng_.state(); }

	// ── Pattern inspection for UI / capture (Drums mode) ────────────────────────

	/// Interpolated map level (0-255) at a step, including the current loop's
	/// perturbation. Returns 0 in Euclidean mode.
	uint8_t levelAt(uint8_t step, uint8_t bank) const;
	/// True if levelAt() exceeds the bank's density threshold.
	bool fireAt(uint8_t step, uint8_t bank) const;
	/// True if the hit at that step would be accented.
	bool accentAt(uint8_t step, uint8_t bank) const;

	/// Simulate the next numSteps (max kStepsPerPattern) step evaluations without
	/// disturbing this engine's state, so the UI can draw the pattern that is about
	/// to play (including Euclidean part counters and loop-wrap perturbations).
	/// Events are returned in chronological order; they are exactly the events the
	/// live engine would emit, including the current step's evaluation if it is still
	/// pending (pulse() == 0).
	void simulateNextSteps(uint8_t numSteps, TopoStepEvent* outEvents) const;

private:
	void evaluateStep(TopoStepEvent& ev);
	void evaluateDrums(TopoStepEvent& ev);
	void evaluateEuclidean(TopoStepEvent& ev);
	uint8_t readDrumMap(uint8_t step, uint8_t bank, uint8_t x, uint8_t y) const;

	TopoSettings settings_;
	TopoMode mode_{TopoMode::Drums};
	bool swing_{false};
	bool gateMode_{false};

	uint8_t step_{0};
	uint8_t pulse_{0};
	uint8_t euclideanStep_[kNumBanks]{0, 0, 0};
	uint8_t perturbation_[kNumBanks]{0, 0, 0};
	bool noiseBit_{false};
	TopoRng rng_;
};

} // namespace deluge::model::topo
