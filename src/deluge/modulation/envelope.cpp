/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "modulation/envelope.h"
#include "util/functions.h" // getDecay4/8, interpolateTable(Inverse), multiply_32x32_rshift32, add_saturate
#include "util/lookuptables/lookuptables.h" // decayTableSmall4
#include "util/waves.h"                     // getSine
#include <algorithm>
#include <limits>

namespace {
constexpr uint32_t kStageEnd = 1u << 23;   ///< A stage completes once its position accumulator reaches this.
constexpr int32_t kFullScale = 2147483647; ///< q31 max — a fully-open envelope level.
constexpr int32_t kHalfScale = 1073741824; ///< 2^30 — the mid level (the output's zero-centre / the sine DC offset).
constexpr int32_t kOff = std::numeric_limits<int32_t>::min(); ///< The centred output of a silent (level-0) envelope.

/// Map an uncentred level (0..kFullScale) to the centred output range (about 0).
constexpr int32_t centred(int32_t level) {
	return (level - kHalfScale) << 1;
}
} // namespace

int32_t Envelope::render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate, uint32_t sustain_level,
                         uint32_t release_rate, const uint16_t* release_table) {
	// Drive the stage machine: step the current stage for this block, then either emit its level,
	// emit the off sentinel, or (attack→decay only) advance into the next stage and step again.
	while (true) {
		StepOutcome outcome;
		switch (stage_) {
		case Stage::ATTACK:
			outcome = stepAttack(attack_rate, num_samples);
			break;
		case Stage::DECAY:
			outcome = stepDecay(decay_rate, sustain_level, num_samples);
			break;
		case Stage::SUSTAIN:
			outcome = stepSustain(sustain_level, num_samples);
			break;
		case Stage::RELEASE:
			outcome = stepRelease(release_rate, num_samples, release_table);
			break;
		case Stage::FAST_RELEASE:
			outcome = stepFastRelease(release_rate, num_samples);
			break;
		default: // OFF / HOLD (unused)
			outcome = StepOutcome::EMIT_OFF;
			break;
		}

		if (outcome == StepOutcome::ADVANCE) {
			continue;
		}
		if (outcome == StepOutcome::EMIT_OFF) {
			return kOff;
		}
		return centred(current_level_);
	}
}

Envelope::StepOutcome Envelope::stepAttack(uint32_t attack_rate, uint32_t num_samples) {
	// Increment the position *before* taking a value, so a high rate can skip the attack entirely.
	stage_position_ += attack_rate * num_samples;
	if (stage_position_ >= kStageEnd) {
		stage_position_ = 0;
		setState(Stage::DECAY);
		return StepOutcome::ADVANCE; // Run decay this same block.
	}
	current_level_ = kFullScale - getDecay4(stage_position_, 23); // Makes a curved attack.
	current_level_ = std::max(current_level_, (int32_t)1);
	return StepOutcome::EMIT_LEVEL;
}

Envelope::StepOutcome Envelope::stepDecay(uint32_t decay_rate, uint32_t sustain_level, uint32_t num_samples) {
	// Smooth toward the sustain target: modulating sustain (e.g. with aftertouch) would otherwise cause
	// un-smoothed jumps downstream, because params don't smooth the envelope value and envelopes are
	// rendered before the other params (including their own sustain) are calculated. Doing it properly
	// would mean patch+smooth expression→envelope, then render the envelope, then everything else —
	// too involved for now, and this is close enough since we'd usually need to smooth it anyway.
	smoothed_sustain_ =
	    add_saturate(smoothed_sustain_, num_samples * (((int32_t)sustain_level - smoothed_sustain_) >> 9));
	current_level_ =
	    smoothed_sustain_ + multiply_32x32_rshift32(getDecay8(stage_position_, 23), kFullScale - smoothed_sustain_) * 2;

	stage_position_ += decay_rate * num_samples;

	if (stage_position_ >= kStageEnd) {
		setState(Stage::SUSTAIN);
	}
	return StepOutcome::EMIT_LEVEL;
}

Envelope::StepOutcome Envelope::stepSustain(uint32_t sustain_level, uint32_t num_samples) {
	smoothed_sustain_ =
	    add_saturate(smoothed_sustain_, num_samples * (((int32_t)sustain_level - smoothed_sustain_) >> 9));
	current_level_ = smoothed_sustain_;
	// A zero sustain means there's nothing to hold — we may as well be off already.
	if (sustain_level == 0) {
		setState(Stage::OFF);
	}
	else if (ignored_note_off_) {
		unconditionalRelease();
	}
	return StepOutcome::EMIT_LEVEL;
}

Envelope::StepOutcome Envelope::stepRelease(uint32_t release_rate, uint32_t num_samples,
                                            const uint16_t* release_table) {
	stage_position_ += release_rate * num_samples;
	if (stage_position_ >= kStageEnd) {
		setState(Stage::OFF);
		current_level_ = 0;
		return StepOutcome::EMIT_OFF;
	}
	current_level_ =
	    multiply_32x32_rshift32(interpolateTable(stage_position_, 23, release_table), level_at_stage_entry_) << 1;
	return StepOutcome::EMIT_LEVEL;
}

Envelope::StepOutcome Envelope::stepFastRelease(uint32_t release_rate, uint32_t num_samples) {
	if (fast_release_rate_ < 2 * release_rate) {
		release_rate = 2 * release_rate;
		fast_release_rate_ = release_rate;
	}
	stage_position_ += fast_release_rate_ * num_samples;
	if (stage_position_ >= kStageEnd) {
		setState(Stage::OFF);
		return StepOutcome::EMIT_OFF;
	}
	// A sine-shaped release: less high-frequency content than a linear ramp, and sounds a touch better
	// on deep bass samples.
	current_level_ = multiply_32x32_rshift32((getSine(stage_position_ + (kStageEnd >> 1), 24) >> 1) + kHalfScale,
	                                         level_at_stage_entry_)
	                 << 1;
	return StepOutcome::EMIT_LEVEL;
}

int32_t Envelope::noteOn(bool directly_to_decay) {
	ignored_note_off_ = false;
	stage_position_ = 0;
	if (!directly_to_decay) {
		setState(Stage::ATTACK);
		current_level_ = 0;
	}
	else {
		setState(Stage::DECAY);
		current_level_ = kFullScale;
	}

	return centred(current_level_);
}

int32_t Envelope::noteOn(bool directly_to_decay, int32_t initial_sustain) {
	smoothed_sustain_ = initial_sustain;
	return noteOn(directly_to_decay);
}

void Envelope::noteOff(bool has_sustain) {
	if (!has_sustain) {
		ignored_note_off_ = true;
	}
	else if (stage_ < Stage::RELEASE) { // Could we already be in a release stage? Probably not, but just in case.
		unconditionalRelease();
	}
}

void Envelope::setState(Stage new_stage) {
	stage_ = new_stage;
	stage_changed_ = true;
}

void Envelope::unconditionalOff() {
	level_at_stage_entry_ = current_level_;
	setState(Stage::OFF);
}

void Envelope::unconditionalRelease(Stage release_stage, uint32_t fast_release_rate) {
	fast_release_rate_ = fast_release_rate;

	if (stage_ != release_stage) {
		setState(release_stage);
		stage_position_ = 0;
		level_at_stage_entry_ = current_level_;
	}
}

void Envelope::resumeAttack(int32_t resume_level) {
	if (stage_ == Stage::ATTACK) {
		stage_position_ = interpolateTableInverse(kFullScale - resume_level, 23, decayTableSmall4);
	}
}
