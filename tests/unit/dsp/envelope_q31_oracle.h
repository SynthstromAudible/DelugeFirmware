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
#pragma once

// next's Q31 envelope as it was before it moved onto the segment engine (src/synth/modulation/envelope.{h,cpp}),
// kept verbatim apart from its name and its definitions made inline: the classic curve's oracle. Do not edit.

#include "definitions_cxx.hpp"
#include "dsp/core/fixed_point.h"
#include "dsp/fixed/interpolation/table_interpolation.h" // get_decay4/8, kLinearTableInterpolator
#include "dsp/fixed/osc/sine_table.h"                    // kSineTable (sine-shaped fast-release)
#include "dsp/fixed/tables/decay_small.h"
#include <algorithm>
#include <cstdint>

namespace deluge::test {

inline constexpr uint32_t kOracleSoftCullIncrement = 65536;

class EnvelopeQ31Oracle {
public:
	/// The ADSR stage. Values are deliberate and ordered: @ref deluge::synth::Voice::priority_rating packs the
	/// stage into 3 bits (later stages = lower voice priority), so the numbering must not change.
	enum class Stage : uint8_t {
		ATTACK,
		HOLD, // Reserved (not yet rendered); kept so the values below stay put.
		DECAY,
		SUSTAIN,
		RELEASE,
		FAST_RELEASE,
		OFF,
	};
	static constexpr int32_t kNumStages = static_cast<int32_t>(Stage::OFF) + 1;

	EnvelopeQ31Oracle() = default;

	/// Advance by @p num_samples and return the (centred) envelope value. @p attack_rate / @p
	/// decay_rate / @p release_rate are per-sample position increments; @p sustain_level is the
	/// target level (not a rate); @p release_table shapes the release curve. The stage machine
	/// self-advances (ATTACK→DECAY→SUSTAIN, RELEASE→OFF, …) purely from the accumulated position.
	deluge::dsp::Q<31> render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
	                          deluge::dsp::Q<31> sustain_level, uint32_t release_rate, const uint16_t* release_table);

	/// Start a note. @p directly_to_decay skips the attack stage (for very fast attacks).
	deluge::dsp::Q<31> note_on(bool directly_to_decay);
	/// Start a note, seeding the sustain tracker with @p initial_sustain (the patched sustain level
	/// at note-on, so the first decay/sustain block doesn't smooth up from a stale value).
	deluge::dsp::Q<31> note_on(bool directly_to_decay, deluge::dsp::Q<31> initial_sustain);

	/// Release the note. @p has_sustain is the caller's decision (e.g. the patched sustain is
	/// audible): if false the note-off is ignored (held until the sustain stage resolves it);
	/// otherwise the envelope enters its release stage.
	void note_off(bool has_sustain);

	void unconditional_release(Stage release_stage = Stage::RELEASE,
	                           uint32_t fast_release_rate = kOracleSoftCullIncrement);
	void unconditional_off();
	/// Re-enter the attack stage at the position matching @p resume_level (legato/voice-reuse, so the
	/// attack resumes from the level the previous note left off at rather than clicking to 0).
	void resume_attack(deluge::dsp::Q<31> resume_level);

	// --- queries (the owner reads these for note/voice decisions; the envelope owns the data) ---

	/// The current ADSR stage.
	[[nodiscard]] Stage stage() const { return stage_; }
	/// The current (uncentred, 0..2^31-1) envelope value — e.g. captured for @ref resumeAttack.
	[[nodiscard]] deluge::dsp::Q<31> output_level() const { return current_level_; }
	/// The fast-release per-sample increment (the voice-cull paths compare against it).
	[[nodiscard]] uint32_t fast_release_rate() const { return fast_release_rate_; }
	/// True if the note has just started its attack and not yet advanced.
	[[nodiscard]] bool is_at_attack_start() const { return stage_ == Stage::ATTACK && stage_position_ == 0; }

	/// True once per stage change: the envelope reports transitions so the owner can keep its own
	/// voice-cull timestamp without the envelope reaching for any engine global. Returns and clears.
	bool take_stage_changed() {
		bool changed = stage_changed_;
		stage_changed_ = false;
		return changed;
	}

	/// @brief Seed the output level directly. **Transitional seam:** the voice must give the envelope
	/// a value before the envelope's own params have been patched (the voice's pre-patch ordering),
	/// and snaps it to full when releasing a not-yet-started attack. This bypasses the stage machine
	/// and exists only until the ControlBlock work (Phase 2 step 4) removes the ordering dependency.
	void set_output_level(deluge::dsp::Q<31> level) { current_level_ = level; }

private:
	void set_state(Stage new_stage);

	/// What a per-stage step wants the render loop to do with its result.
	enum class StepOutcome {
		EMIT_LEVEL, ///< Emit the (centred) current level for this block.
		EMIT_OFF,   ///< The envelope has finished — emit the off/silence sentinel.
		ADVANCE,    ///< This stage completed mid-block; re-dispatch into the new stage (attack→decay).
	};
	// Each step advances the shared state for one block of its stage and returns how to finish the
	// block. Split out of one big switch so each stage's (idiosyncratic) curve math is named and
	// isolated; the values/arithmetic are unchanged.
	StepOutcome step_attack(uint32_t attack_rate, uint32_t num_samples);
	StepOutcome step_decay(uint32_t decay_rate, deluge::dsp::Q<31> sustain_level, uint32_t num_samples);
	StepOutcome step_sustain(deluge::dsp::Q<31> sustain_level, uint32_t num_samples);
	StepOutcome step_release(uint32_t release_rate, uint32_t num_samples, const uint16_t* release_table);
	StepOutcome step_fast_release(uint32_t release_rate, uint32_t num_samples);

	uint32_t stage_position_;                 ///< Phase within the current stage; the stage ends when it reaches 2^23.
	Stage stage_;                             ///< The current ADSR stage.
	deluge::dsp::Q<31> current_level_;        ///< The current (uncentred) envelope output level.
	deluge::dsp::Q<31> level_at_stage_entry_; ///< The level when the current stage began (scales the release curves).
	uint32_t fast_release_rate_{1024};        ///< Per-sample increment used by the fast-release stage.
	bool ignored_note_off_;                   ///< A note-off arrived while there was no audible sustain to release.
	deluge::dsp::Q<31> smoothed_sustain_{};
	bool stage_changed_{false};
};

using deluge::dsp::Q;
using deluge::dsp::fixed::get_decay4;
using deluge::dsp::fixed::get_decay8;
using deluge::dsp::fixed::kLinearTableInterpolator;
using deluge::dsp::fixed::kSineTable;

namespace q31_oracle_detail {
inline constexpr uint32_t kStageEnd = 1u << 23;   ///< A stage completes once its position reaches this.
inline constexpr Q<31> kFullScale = Q<31>::max(); ///< A fully-open envelope level.
inline constexpr Q<31> kHalfScale =
    deluge::dsp::q<31>(0.5);                ///< The mid level (the output's zero-centre, the sine's DC).
inline constexpr Q<31> kOff = Q<31>::min(); ///< The centred output of a silent (level-0) envelope.

/// Map an uncentred level (0 to full scale) to the centred output range (about 0): less a half, doubled.
inline constexpr Q<31> centred(Q<31> level) {
	return scale_shl_wrap<1>(sub_wrap(level, kHalfScale));
}

/// A stage's position, UQ23 of the stage (kStageEnd is its end), moved on by @p rate for @p num_samples: the integer
/// step in 64 bits, as a patched attack's rate times a block can pass 2^32, and held at the stage's end, as no stage
/// reads its position once it has ended.
inline constexpr uint32_t advanced(uint32_t position, uint32_t rate, uint32_t num_samples) {
	const uint64_t moved = uint64_t{position} + (uint64_t{rate} * num_samples);
	return static_cast<uint32_t>(std::min<uint64_t>(moved, kStageEnd));
}

/// The smoothed sustain moved toward @p sustain_level: an integer step of 1/512 of the distance per sample, the sum
/// saturating.
inline constexpr Q<31> smoothed(Q<31> smoothed_sustain, Q<31> sustain_level, uint32_t num_samples) {
	// Raw: the step is an integer product of the block's length and the shifted distance
	const auto step =
	    Q<31>::from_raw(static_cast<int32_t>(num_samples) * ((sustain_level.raw() - smoothed_sustain.raw()) >> 9));
	return add_sat(smoothed_sustain, step);
}

/// A decay table's value at @p position: its words are Q31 levels.
inline Q<31> decay4(uint32_t position) {
	return Q<31>::from_raw(get_decay4(position, 23));
}
} // namespace q31_oracle_detail
using namespace q31_oracle_detail;

inline Q<31> EnvelopeQ31Oracle::render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
                                       Q<31> sustain_level, uint32_t release_rate, const uint16_t* release_table) {
	// Drive the stage machine: step the current stage for this block, then either emit its level,
	// emit the off sentinel, or (attack→decay only) advance into the next stage and step again.
	while (true) {
		StepOutcome outcome;
		switch (stage_) {
		case Stage::ATTACK:
			outcome = step_attack(attack_rate, num_samples);
			break;
		case Stage::DECAY:
			outcome = step_decay(decay_rate, sustain_level, num_samples);
			break;
		case Stage::SUSTAIN:
			outcome = step_sustain(sustain_level, num_samples);
			break;
		case Stage::RELEASE:
			outcome = step_release(release_rate, num_samples, release_table);
			break;
		case Stage::FAST_RELEASE:
			outcome = step_fast_release(release_rate, num_samples);
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

inline EnvelopeQ31Oracle::StepOutcome EnvelopeQ31Oracle::step_attack(uint32_t attack_rate, uint32_t num_samples) {
	// Increment the position *before* taking a value, so a high rate can skip the attack entirely.
	stage_position_ = advanced(stage_position_, attack_rate, num_samples);
	if (stage_position_ >= kStageEnd) {
		stage_position_ = 0;
		set_state(Stage::DECAY);
		return StepOutcome::ADVANCE; // Run decay this same block.
	}
	current_level_ = sub_wrap(kFullScale, decay4(stage_position_)); // Makes a curved attack.
	// At least one LSB: the attack never emits a zero level
	current_level_ = std::max(current_level_, deluge::dsp::q<31>(0x1p-31));
	return StepOutcome::EMIT_LEVEL;
}

inline EnvelopeQ31Oracle::StepOutcome EnvelopeQ31Oracle::step_decay(uint32_t decay_rate, Q<31> sustain_level,
                                                                    uint32_t num_samples) {
	// Smooth toward the sustain target: modulating sustain (e.g. with aftertouch) would otherwise cause
	// un-smoothed jumps downstream, because params don't smooth the envelope value and envelopes are
	// rendered before the other params (including their own sustain) are calculated. Doing it properly
	// would mean patch+smooth expression→envelope, then render the envelope, then everything else —
	// too involved for now, and this is close enough since we'd usually need to smooth it anyway.
	smoothed_sustain_ = smoothed(smoothed_sustain_, sustain_level, num_samples);
	// The sustain plus the decay curve's share of what lies above it: within full scale. The table's words are Q31
	const auto above =
	    mul_as_wrap<Q<31>>(Q<31>::from_raw(get_decay8(stage_position_, 23)), sub_wrap(kFullScale, smoothed_sustain_));
	current_level_ = add_wrap(smoothed_sustain_, above);

	stage_position_ = advanced(stage_position_, decay_rate, num_samples);

	if (stage_position_ >= kStageEnd) {
		set_state(Stage::SUSTAIN);
	}
	return StepOutcome::EMIT_LEVEL;
}

inline EnvelopeQ31Oracle::StepOutcome EnvelopeQ31Oracle::step_sustain(Q<31> sustain_level, uint32_t num_samples) {
	smoothed_sustain_ = smoothed(smoothed_sustain_, sustain_level, num_samples);
	current_level_ = smoothed_sustain_;
	// A zero sustain means there's nothing to hold — we may as well be off already.
	if (sustain_level == Q<31>{}) {
		set_state(Stage::OFF);
	}
	else if (ignored_note_off_) {
		unconditional_release();
	}
	return StepOutcome::EMIT_LEVEL;
}

inline EnvelopeQ31Oracle::StepOutcome EnvelopeQ31Oracle::step_release(uint32_t release_rate, uint32_t num_samples,
                                                                      const uint16_t* release_table) {
	stage_position_ = advanced(stage_position_, release_rate, num_samples);
	if (stage_position_ >= kStageEnd) {
		set_state(Stage::OFF);
		current_level_ = Q<31>{};
		return StepOutcome::EMIT_OFF;
	}
	// The release table's curve (its interpolation is Q31) times the level the release started from
	current_level_ =
	    mul_as_wrap<Q<31>>(Q<31>::from_raw(kLinearTableInterpolator.interpolate(stage_position_, 23, release_table)),
	                       level_at_stage_entry_);
	return StepOutcome::EMIT_LEVEL;
}

inline EnvelopeQ31Oracle::StepOutcome EnvelopeQ31Oracle::step_fast_release(uint32_t release_rate,
                                                                           uint32_t num_samples) {
	if (fast_release_rate_ < 2 * release_rate) {
		release_rate = 2 * release_rate;
		fast_release_rate_ = release_rate;
	}
	stage_position_ = advanced(stage_position_, fast_release_rate_, num_samples);
	if (stage_position_ >= kStageEnd) {
		set_state(Stage::OFF);
		return StepOutcome::EMIT_OFF;
	}
	// A sine-shaped release: less high-frequency content than a linear ramp, and sounds a touch better
	// on deep bass samples.
	// The Q31 sine from its peak, made unipolar (halved, plus a half), times the level the release started from
	const Q<31> sine = Q<31>::from_raw(kSineTable.value(stage_position_ + (kStageEnd >> 1), 24));
	const Q<31> shape = add_wrap(halve_floor(sine), kHalfScale);
	current_level_ = mul_as_wrap<Q<31>>(shape, level_at_stage_entry_);
	return StepOutcome::EMIT_LEVEL;
}

inline Q<31> EnvelopeQ31Oracle::note_on(bool directly_to_decay) {
	ignored_note_off_ = false;
	stage_position_ = 0;
	if (!directly_to_decay) {
		set_state(Stage::ATTACK);
		current_level_ = Q<31>{};
	}
	else {
		set_state(Stage::DECAY);
		current_level_ = kFullScale;
	}

	return centred(current_level_);
}

inline Q<31> EnvelopeQ31Oracle::note_on(bool directly_to_decay, Q<31> initial_sustain) {
	smoothed_sustain_ = initial_sustain;
	return note_on(directly_to_decay);
}

inline void EnvelopeQ31Oracle::note_off(bool has_sustain) {
	if (!has_sustain) {
		ignored_note_off_ = true;
	}
	else if (stage_ < Stage::RELEASE) { // Could we already be in a release stage? Probably not, but just in case.
		unconditional_release();
	}
}

inline void EnvelopeQ31Oracle::set_state(Stage new_stage) {
	stage_ = new_stage;
	stage_changed_ = true;
}

inline void EnvelopeQ31Oracle::unconditional_off() {
	level_at_stage_entry_ = current_level_;
	set_state(Stage::OFF);
}

inline void EnvelopeQ31Oracle::unconditional_release(Stage release_stage, uint32_t fast_release_rate) {
	fast_release_rate_ = fast_release_rate;

	if (stage_ != release_stage) {
		set_state(release_stage);
		stage_position_ = 0;
		level_at_stage_entry_ = current_level_;
	}
}

inline void EnvelopeQ31Oracle::resume_attack(Q<31> resume_level) {
	if (stage_ == Stage::ATTACK) {
		// Raw: the table's inverse takes the level's word
		stage_position_ = kLinearTableInterpolator.inverse(sub_wrap(kFullScale, resume_level).raw(), 23,
		                                                   deluge::dsp::fixed::tables::kDecaySmall4.data());
	}
}

} // namespace deluge::test
