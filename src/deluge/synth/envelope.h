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

#include "definitions_cxx.hpp"
#include <cstdint>

constexpr uint32_t SOFT_CULL_INCREMENT = 65536;

/// @brief A self-contained ADSR envelope generator: its output is driven purely by sample
/// progression.
///
/// The envelope owns only its own state (stage + position + the value it is tracking). It takes no
/// @c Sound / @c Voice / @c ParamManager / engine globals: the caller supplies the ADSR rates to
/// @ref render each block, drives the note lifecycle with plain values, and reads @ref stage back
/// for its own note/voice decisions. This makes it a freestanding, unit-testable building block.
///
/// Levels are q31 centred around 0 on output; @c attack / @c decay / @c release are per-sample
/// phase-rate increments (the stage advances when the internal position reaches 2^23); @c sustain
/// is a target level the envelope smooths toward.
class Envelope {
public:
	/// The ADSR stage. Values are deliberate and ordered: @ref Voice::getPriorityRating packs the
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

	Envelope() = default;

	/// Advance by @p num_samples and return the (centred) envelope value. @p attack_rate / @p
	/// decay_rate / @p release_rate are per-sample position increments; @p sustain_level is the
	/// target level (not a rate); @p release_table shapes the release curve. The stage machine
	/// self-advances (ATTACK→DECAY→SUSTAIN, RELEASE→OFF, …) purely from the accumulated position.
	int32_t render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate, uint32_t sustain_level,
	               uint32_t release_rate, const uint16_t* release_table);

	/// Start a note. @p directly_to_decay skips the attack stage (for very fast attacks).
	int32_t noteOn(bool directly_to_decay);
	/// Start a note, seeding the sustain tracker with @p initial_sustain (the patched sustain level
	/// at note-on, so the first decay/sustain block doesn't smooth up from a stale value).
	int32_t noteOn(bool directly_to_decay, int32_t initial_sustain);

	/// Release the note. @p has_sustain is the caller's decision (e.g. the patched sustain is
	/// audible): if false the note-off is ignored (held until the sustain stage resolves it);
	/// otherwise the envelope enters its release stage.
	void noteOff(bool has_sustain);

	void unconditionalRelease(Stage release_stage = Stage::RELEASE, uint32_t fast_release_rate = SOFT_CULL_INCREMENT);
	void unconditionalOff();
	/// Re-enter the attack stage at the position matching @p resume_level (legato/voice-reuse, so the
	/// attack resumes from the level the previous note left off at rather than clicking to 0).
	void resumeAttack(int32_t resume_level);

	// --- queries (the owner reads these for note/voice decisions; the envelope owns the data) ---

	/// The current ADSR stage.
	[[nodiscard]] Stage stage() const { return stage_; }
	/// The current (uncentred, 0..2^31-1) envelope value — e.g. captured for @ref resumeAttack.
	[[nodiscard]] int32_t outputLevel() const { return current_level_; }
	/// The fast-release per-sample increment (the voice-cull paths compare against it).
	[[nodiscard]] uint32_t fastReleaseRate() const { return fast_release_rate_; }
	/// True if the note has just started its attack and not yet advanced.
	[[nodiscard]] bool isAtAttackStart() const { return stage_ == Stage::ATTACK && stage_position_ == 0; }

	/// True once per stage change: the envelope reports transitions so the owner can keep its own
	/// voice-cull timestamp without the envelope reaching for any engine global. Returns and clears.
	bool takeStageChanged() {
		bool changed = stage_changed_;
		stage_changed_ = false;
		return changed;
	}

	/// @brief Seed the output level directly. **Transitional seam:** the voice must give the envelope
	/// a value before the envelope's own params have been patched (the voice's pre-patch ordering),
	/// and snaps it to full when releasing a not-yet-started attack. This bypasses the stage machine
	/// and exists only until the ControlBlock work (Phase 2 step 4) removes the ordering dependency.
	void setOutputLevel(int32_t level) { current_level_ = level; }

private:
	void setState(Stage new_stage);

	/// What a per-stage step wants the render loop to do with its result.
	enum class StepOutcome {
		EMIT_LEVEL, ///< Emit the (centred) current level for this block.
		EMIT_OFF,   ///< The envelope has finished — emit the off/silence sentinel.
		ADVANCE,    ///< This stage completed mid-block; re-dispatch into the new stage (attack→decay).
	};
	// Each step advances the shared state for one block of its stage and returns how to finish the
	// block. Split out of one big switch so each stage's (idiosyncratic) curve math is named and
	// isolated; the values/arithmetic are unchanged.
	StepOutcome stepAttack(uint32_t attack_rate, uint32_t num_samples);
	StepOutcome stepDecay(uint32_t decay_rate, uint32_t sustain_level, uint32_t num_samples);
	StepOutcome stepSustain(uint32_t sustain_level, uint32_t num_samples);
	StepOutcome stepRelease(uint32_t release_rate, uint32_t num_samples, const uint16_t* release_table);
	StepOutcome stepFastRelease(uint32_t release_rate, uint32_t num_samples);

	uint32_t stage_position_;          ///< Phase within the current stage; the stage ends when it reaches 2^23.
	Stage stage_;                      ///< The current ADSR stage.
	int32_t current_level_;            ///< The current (uncentred) envelope output level.
	int32_t level_at_stage_entry_;     ///< The level when the current stage began (scales the release curves).
	uint32_t fast_release_rate_{1024}; ///< Per-sample increment used by the fast-release stage.
	bool ignored_note_off_;            ///< A note-off arrived while there was no audible sustain to release.
	int32_t smoothed_sustain_{0};
	bool stage_changed_{false};
};
