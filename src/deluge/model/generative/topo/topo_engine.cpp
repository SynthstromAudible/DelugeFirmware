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
 * "Topo Drums" generative drum-pattern engine - ported from the pattern generator
 * of the original Mutable Instruments Grids firmware (Copyright 2011/2012 Emilie
 * Gillet, GPLv3). See topo_engine.h for port notes.
 */

#include "model/generative/topo/topo_engine.h"

namespace deluge::model::topo {

using detail::u8Mix;
using detail::u8u8MulShift8;

// Original TickClock(): Evaluate() runs once per incoming clock pulse at the
// *current* step, then the pulse/step counters advance. We reproduce that,
// treating each pulse as one clock tick (the hardware's 24 PPQN mode).
TopoStepEvent TopoEngine::advance(uint32_t numPulses) {
	TopoStepEvent ev;

	for (uint32_t p = 0; p < numPulses; ++p) {
		// The original refreshes the RNG (and its random output bit) once per pulse.
		rng_.update();
		noiseBit_ = (rng_.state() & 0x80) != 0;

		if (pulse_ == 0) {
			// Step boundary: evaluate the pattern for the current step.
			++ev.stepsCrossed;
			ev.step = step_;
			if (step_ == 0) {
				ev.loopWrapped = true;
			}
			evaluateStep(ev);
		}

		// Advance the clock.
		++pulse_;
		if (pulse_ >= kPulsesPerStep) {
			pulse_ -= kPulsesPerStep;
			if (!(step_ & 1)) {
				// Euclidean part counters advance at sixteenth-note intervals.
				for (uint8_t i = 0; i < kNumBanks; ++i) {
					++euclideanStep_[i];
				}
			}
			++step_;
			if (step_ >= kStepsPerPattern) {
				step_ -= kStepsPerPattern;
			}
		}
	}

	return ev;
}

void TopoEngine::evaluateStep(TopoStepEvent& ev) {
	if (mode_ == TopoMode::Euclidean) {
		evaluateEuclidean(ev);
	}
	else {
		evaluateDrums(ev);
	}
}

/* static */
uint8_t TopoEngine::readDrumMap(uint8_t step, uint8_t bank, uint8_t x, uint8_t y) const {
	uint8_t i = x >> 6;
	uint8_t j = y >> 6;
	const uint8_t* a_map = kPatternNodes[kDrumMap[i][j]];
	const uint8_t* b_map = kPatternNodes[kDrumMap[i + 1][j]];
	const uint8_t* c_map = kPatternNodes[kDrumMap[i][j + 1]];
	const uint8_t* d_map = kPatternNodes[kDrumMap[i + 1][j + 1]];
	uint16_t offset = static_cast<uint16_t>(bank) * kStepsPerPattern + step;
	uint8_t a = a_map[offset];
	uint8_t b = b_map[offset];
	uint8_t c = c_map[offset];
	uint8_t d = d_map[offset];
	return u8Mix(u8Mix(a, b, static_cast<uint8_t>(x << 2)), u8Mix(c, d, static_cast<uint8_t>(x << 2)),
	             static_cast<uint8_t>(y << 2));
}

void TopoEngine::evaluateDrums(TopoStepEvent& ev) {
	// At the beginning of a pattern, decide on perturbation levels.
	if (step_ == 0) {
		uint8_t randomness = swing_ ? 0 : (settings_.randomness >> 2);
		for (uint8_t i = 0; i < kNumBanks; ++i) {
			perturbation_[i] = u8u8MulShift8(rng_.getByte(), randomness);
		}
	}

	uint8_t instrument_mask = 1;
	for (uint8_t i = 0; i < kNumBanks; ++i) {
		uint8_t level = readDrumMap(step_, i, settings_.x, settings_.y);
		if (level < 255 - perturbation_[i]) {
			level += perturbation_[i];
		}
		else {
			// The sequencer from Anushri uses a weird clipping rule here - kept for
			// identical behaviour to the original.
			level = 255;
		}
		uint8_t threshold = ~settings_.density[i];
		if (level > threshold) {
			if (level > settings_.accentThreshold) {
				ev.accentMask |= instrument_mask;
			}
			ev.fireMask |= instrument_mask;
		}
		instrument_mask <<= 1;
	}
}

void TopoEngine::evaluateEuclidean(TopoStepEvent& ev) {
	// Refresh only on sixteenth notes.
	if (step_ & 1) {
		return;
	}

	// In Euclidean mode, the x / y / randomness settings become the three banks'
	// pattern lengths (the original's options union trick).
	const uint8_t lengths[kNumBanks] = {settings_.x, settings_.y, settings_.randomness};

	uint8_t instrument_mask = 1;
	for (uint8_t i = 0; i < kNumBanks; ++i) {
		uint8_t length = (lengths[i] >> 3) + 1;
		uint8_t density = settings_.density[i] >> 3;
		uint16_t address = static_cast<uint16_t>(length - 1) * 32 + density;
		while (euclideanStep_[i] >= length) {
			euclideanStep_[i] -= length;
		}
		uint32_t step_mask = 1u << static_cast<uint32_t>(euclideanStep_[i]);
		uint32_t pattern_bits = kEuclideanPatterns[address];
		if (pattern_bits & step_mask) {
			ev.fireMask |= instrument_mask;
		}
		if (euclideanStep_[i] == 0) {
			// Cycle-start "reset" bit (the original exposed it on a separate output).
			ev.accentMask |= instrument_mask;
		}
		instrument_mask <<= 1;
	}
}

int8_t TopoEngine::swingAmount() const {
	if (swing_ && mode_ == TopoMode::Drums) {
		int8_t value = u8u8MulShift8(settings_.randomness, 42 + 1);
		return !(step_ & 2) ? value : -value;
	}
	return 0;
}

uint8_t TopoEngine::levelAt(uint8_t step, uint8_t bank) const {
	if (mode_ != TopoMode::Drums || step >= kStepsPerPattern || bank >= kNumBanks) {
		return 0;
	}
	uint8_t level = readDrumMap(step, bank, settings_.x, settings_.y);
	uint8_t perturbation = perturbation_[bank];
	if (level < 255 - perturbation) {
		level += perturbation;
	}
	else {
		level = 255;
	}
	return level;
}

bool TopoEngine::fireAt(uint8_t step, uint8_t bank) const {
	if (mode_ != TopoMode::Drums || bank >= kNumBanks) {
		return false;
	}
	return levelAt(step, bank) > static_cast<uint8_t>(~settings_.density[bank]);
}

bool TopoEngine::accentAt(uint8_t step, uint8_t bank) const {
	if (mode_ != TopoMode::Drums || bank >= kNumBanks) {
		return false;
	}
	uint8_t level = levelAt(step, bank);
	return level > static_cast<uint8_t>(~settings_.density[bank]) && level > settings_.accentThreshold;
}

void TopoEngine::simulateNextSteps(uint8_t numSteps, TopoStepEvent* outEvents) const {
	if (!outEvents) {
		return;
	}
	if (numSteps > kStepsPerPattern) {
		numSteps = kStepsPerPattern;
	}

	TopoEngine sim = *this; // snapshot all state, including the RNG
	uint8_t produced = 0;

	// Align to the next step boundary first (no events are emitted while aligning, but
	// the RNG advances exactly as the live engine's would), so the simulated events are
	// precisely the ones the live engine is about to produce. When pulse() == 0 the
	// current step has not been evaluated yet, and its evaluation is the first event.
	if (sim.pulse_ != 0) {
		sim.advance(kPulsesPerStep - sim.pulse_);
	}
	while (produced < numSteps) {
		TopoStepEvent ev = sim.advance(kPulsesPerStep);
		if (ev.stepsCrossed) {
			outEvents[produced++] = ev;
		}
	}
}

} // namespace deluge::model::topo
