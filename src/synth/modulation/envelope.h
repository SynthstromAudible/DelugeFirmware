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
 */

#pragma once

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/envelope/stage.h"
#include "synth/modulation/envelope_state.h"

#include <cstdint>

/// @brief The fast release a soft cull runs at, in position steps a sample.
constexpr uint32_t kSoftCullIncrement = 65536;

/// @brief A classic synth voice's envelope: main's curve and modes, in integers.
///
/// Freestanding: the caller gives the params each block, drives the note with plain values and reads the stage back.
/// The params are main's: attack, decay and release are position steps a sample of a stage 2^23 long, and sustain is a
/// level.
///
/// A stage is a position (dsp::fixed::EnvelopeStage) whose curve (dsp::fixed::EnvelopeCurve) gives the level. It ends
/// at its exact sample within the block, and the next runs on for the rest. The decay is a unit curve scaled onto the
/// smoothed sustain. AR and MPE, while held, slew by main's rule once a block.
///
/// The classic instrument's alone: its place is `instruments/classic`, once the voice that holds it is templated over
/// its backend.
class Envelope {
public:
	/// The stage. Values are deliberate and ordered: @ref deluge::synth::Voice::priority_rating packs the stage into 3
	/// bits (later stages = lower voice priority), so the numbering must not change.
	enum class Stage : uint8_t {
		ATTACK,
		HOLD, ///< AHD's hold
		DECAY,
		SUSTAIN, ///< ADSR's sustain; AR and MPE while held
		RELEASE,
		FAST_RELEASE,
		OFF,
	};
	static constexpr int32_t kNumStages = static_cast<int32_t>(Stage::OFF) + 1;

	/// A stage's length in position steps.
	static constexpr uint32_t kStageEnd = deluge::dsp::fixed::kEnvelopeStageEnd;
	/// AHD's hold runs this much faster than a decay at the same param (main's kAhdHoldSpeedup).
	static constexpr uint32_t kAhdHoldSpeedup = 8;

	/// @brief Advances @p num_samples and returns the centred level.
	/// @param follow_input What MPE mode follows, as its patch source word; other modes ignore it.
	deluge::dsp::Q<31> render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
	                          deluge::dsp::Q<31> sustain_level, uint32_t release_rate,
	                          const deluge::synth::EnvelopeState& state, deluge::dsp::Q<31> follow_input);
	/// @brief An audio clip's envelope: classic ADSR with its shallower release (kAudioClipEighths).
	deluge::dsp::Q<31> render_audio_clip(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
	                                     deluge::dsp::Q<31> sustain_level, uint32_t release_rate);

	/// @brief Starts a classic ADSR note. @p directly_to_decay skips the attack.
	deluge::dsp::Q<31> note_on(bool directly_to_decay);
	/// @brief As above, the smoothed sustain starting at @p initial_sustain.
	deluge::dsp::Q<31> note_on(bool directly_to_decay, deluge::dsp::Q<31> initial_sustain);
	/// @brief Starts a note under @p state's mode, which holds until the next note-on. AR and MPE always attack.
	deluge::dsp::Q<31> note_on(bool directly_to_decay, deluge::dsp::Q<31> initial_sustain,
	                           const deluge::synth::EnvelopeState& state);
	/// @brief Releases the note. AHD ignores it (a one-shot); AR and MPE always release; ADSR releases when
	/// @p has_sustain, and otherwise lets the sustain stage resolve it.
	void note_off(bool has_sustain);
	/// @brief Enters @p release_stage from the current level; a fast release runs at @p fast_release_rate or twice
	/// the release rate, whichever is faster.
	void unconditional_release(Stage release_stage = Stage::RELEASE, uint32_t fast_release_rate = kSoftCullIncrement);
	/// @brief Stops at once.
	void unconditional_off();
	/// @brief On a note-on, carries on from @p resume_level: the attack from where its curve reaches it, AR and MPE
	/// from it.
	void resume_attack(deluge::dsp::Q<31> resume_level);

	/// @brief The stage.
	[[nodiscard]] Stage stage() const { return stage_; }
	/// @brief The uncentred level, 0 to full scale.
	[[nodiscard]] deluge::dsp::Q<31> output_level() const { return level_; }
	/// @brief The fast release's per-sample step.
	[[nodiscard]] uint32_t fast_release_rate() const { return fast_release_rate_; }
	/// @brief Whether the note has just started its attack and not yet moved.
	[[nodiscard]] bool is_at_attack_start() const { return stage_ == Stage::ATTACK && position_.position == 0; }
	/// @brief True once per stage change; returns and clears.
	bool take_stage_changed() {
		const bool changed = stage_changed_;
		stage_changed_ = false;
		return changed;
	}
	/// @brief Seeds the level directly. **Transitional seam:** the voice gives the envelope a value before its own
	/// params are patched, and snaps it to full when releasing a not-yet-started attack.
	void set_output_level(deluge::dsp::Q<31> level) { level_ = level; }

private:
	/// Each stage's position steps a sample, as this block's params give them.
	struct Rates {
		uint32_t attack;
		uint32_t hold;
		uint32_t decay;
		uint32_t release;
	};

	deluge::dsp::Q<31> run(uint32_t num_samples, const Rates& rates, deluge::dsp::Q<31> sustain_level,
	                       deluge::synth::EnvelopeEighths eighths);
	deluge::dsp::Q<31> run_mode(uint32_t num_samples, Rates rates, deluge::dsp::Q<31> sustain_level,
	                            deluge::synth::EnvelopeEighths eighths, deluge::dsp::Q<31> follow_input);
	deluge::dsp::Q<31> follow(uint32_t num_samples, const Rates& rates, deluge::dsp::Q<31> sustain_level,
	                          int8_t eighths, deluge::dsp::Q<31> follow_input);
	/// The stage this block runs at, its rate; a stage with none is 0.
	[[nodiscard]] uint32_t rate_of(Stage stage, const Rates& rates) const;
	/// The running stage, at @p rate, ran @p over steps past its end: goes on to the stage the block ends in.
	void go_on(uint64_t over, uint32_t rate, const Rates& rates);
	void smooth_sustain(uint32_t num_samples, deluge::dsp::Q<31> sustain_level);
	void set_state(Stage new_stage);

	deluge::dsp::fixed::EnvelopeStage position_; ///< in the running stage
	deluge::dsp::Q<31> level_{};                 ///< 0 to full scale
	deluge::dsp::Q<31> from_{};                  ///< the level a release started at
	deluge::dsp::Q<31> smoothed_sustain_{};
	int32_t level_rest_{
	    0}; ///< a slew's level below `level_`'s last bit, in 2^-62: a slow one moves less than a bit a block
	uint32_t fast_release_rate_{1024};
	Stage stage_{Stage::OFF};
	deluge::synth::EnvelopeMode mode_{deluge::synth::EnvelopeMode::ADSR};
	int8_t curve_{static_cast<int8_t>(deluge::synth::kClassicCurve)}; ///< the note's curve control, for a resume
	bool ignored_note_off_{false};
	bool stage_changed_{false};
};
