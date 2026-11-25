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
#include "dsp/float/envelope/curve.h"
#include "dsp/float/envelope/envelope.h"
#include "synth/modulation/envelope.h" // kSoftCullIncrement
#include "synth/modulation/envelope_state.h"

#include <cstdint>

/// @brief A synth envelope with main's curve and modes, rendered by the float segment engine (fp::Envelope).
///
/// Freestanding, as @ref Envelope: the caller gives the params each block, drives the note with plain values and reads
/// the stage back. The params are main's: attack, decay and release are per-sample position steps of a stage 2^23
/// long, so a stage lasts `2^23 / rate` samples, and sustain is a level.
///
/// Attack, AHD's hold, decay, release and the fast release are timed segments of the engine, re-timed every block from
/// their params so a modulated rate carries the stage on from where it is; a stage ends at its exact sample within the
/// block and the next runs on for the rest. The decay runs as main's unit curve, scaled onto the smoothed sustain. AR
/// and MPE, while held, slew by main's rule once a block. Every level is main's curve (fp::Curve) at the stage's
/// elapsed fraction, its k from the envelope's one curve control.
class SegmentEnvelope {
public:
	/// The stage. Values are deliberate and ordered as @ref Envelope's: a voice packs it into 3 bits.
	enum class Stage : uint8_t {
		ATTACK,
		HOLD, ///< AHD's hold
		DECAY,
		SUSTAIN, ///< ADSR's sustain; AR and MPE while held
		RELEASE,
		FAST_RELEASE,
		OFF,
	};

	/// A stage's length in position steps.
	static constexpr uint32_t kStageEnd = 1U << 23;
	/// AHD's hold runs this much faster than a decay at the same param (main's kAhdHoldSpeedup).
	static constexpr uint32_t kAhdHoldSpeedup = 8;

	/// @brief Advances @p num_samples and returns the centred level.
	/// @param follow_input What MPE mode follows, as its patch source word; other modes ignore it.
	deluge::dsp::Q<31> render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
	                          deluge::dsp::Q<31> sustain_level, uint32_t release_rate,
	                          const deluge::synth::EnvelopeState& state, deluge::dsp::Q<31> follow_input);
	/// @brief An audio clip's envelope: classic ADSR with its shallower release (fp::kAudioClipShape).
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
	[[nodiscard]] deluge::dsp::Q<31> output_level() const;
	/// @brief The fast release's per-sample step.
	[[nodiscard]] uint32_t fast_release_rate() const { return fast_release_rate_; }
	/// @brief Whether the note has just started its attack and not yet moved.
	[[nodiscard]] bool is_at_attack_start() const { return stage_ == Stage::ATTACK && segment_.elapsed == 0.f; }
	/// @brief True once per stage change; returns and clears.
	bool take_stage_changed() {
		const bool changed = stage_changed_;
		stage_changed_ = false;
		return changed;
	}
	/// @brief Seeds the level directly, as @ref Envelope::set_output_level.
	void set_output_level(deluge::dsp::Q<31> level);

private:
	/// The program's segments, the same for every mode; the mode sets where each leads.
	enum class Segment : uint8_t {
		ATTACK,
		HOLD,
		DECAY,
		SUSTAIN,
		RELEASE,
		FAST_RELEASE,
	};

	deluge::dsp::Q<31> render_shaped(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
	                                 deluge::dsp::Q<31> sustain_level, uint32_t release_rate,
	                                 const deluge::dsp::fp::CurveShape& shape, deluge::dsp::Q<31> follow_input);
	deluge::dsp::Q<31> follow(uint32_t num_samples, uint32_t attack_rate, deluge::dsp::Q<31> sustain_level,
	                          uint32_t release_rate, const deluge::dsp::fp::CurveShape& shape,
	                          deluge::dsp::Q<31> follow_input);
	void build_program();
	[[nodiscard]] deluge::dsp::fp::Segment<float>& slot(Segment s) { return program_.segments[static_cast<size_t>(s)]; }
	void enter(Segment s);
	void set_state(Stage new_stage);

	deluge::dsp::fp::Envelope::Coefficients program_{};
	deluge::dsp::fp::Envelope::State segment_{};
	deluge::synth::EnvelopeMode mode_{deluge::synth::EnvelopeMode::ADSR};
	deluge::dsp::fp::CurveShape shape_{}; ///< the last render's, for a resume
	float level_{0.f};                    ///< the level, 0 to 1
	float smoothed_sustain_{0.f};
	uint32_t fast_release_rate_{1024};
	Stage stage_{Stage::OFF};
	bool ignored_note_off_{false};
	bool stage_changed_{false};
};
