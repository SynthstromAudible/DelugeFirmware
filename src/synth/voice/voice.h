/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "dsp-next/fixed/filter/filter_set.h"
#include "effects/saturation.h"
#include "modulation/params/param.h"
#include "synth/engines/engine_set.h"
#include "synth/engines/source_levels.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_block.h"
#include "synth/matrix/matrix_fixed.h"
#include "synth/modulation/envelope.h"
#include "synth/modulation/lfo.h"
#include "synth/modulation/param_plane.h"
#include "synth/modulation/patch_routing.h" // GLOBALITY_*
#include "synth/voice/sound_snapshot.h"
#include "synth/voice/unison_part.h"
#include "synth/voice/voice_config.h"
#include "synth/voice/voice_inputs.h"
#include <bitset>
#include <compare>
#include <memory>
#include <span>

namespace deluge::dsp {
struct StereoSample;
} // namespace deluge::dsp

namespace deluge::synth {

/// @brief One voice of a synth sound: its sources in each unison part, envelopes, local LFOs, modulation and filters,
/// played from what it is handed — the sound's snapshot and per-block config, a NoteSetup at each note, and the
/// sources' ports. The app's VoiceSlot owns one, with the ports and the playback guides.
class Voice final {
public:
	Voice();
	Voice(const Voice&) = delete;
	Voice& operator=(const Voice&) = delete;
	Voice(Voice&&) = delete;
	Voice& operator=(Voice&&) = delete;
	~Voice() = default;
	// Each unison part's source slots. Public: the app's slicer, sample marker editor and unison-count change read
	// the parts' samples through it.
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	std::array<UnisonPart, kMaxNumVoicesUnison> unison_parts{};

	/// Envelope @p e: 0 the amplitude envelope, 1 the second.
	[[nodiscard]] Envelope& envelope(int32_t e) { return envelopes_[e]; }
	[[nodiscard]] const Envelope& envelope(int32_t e) const { return envelopes_[e]; }
	/// The playing note, after the arpeggiator.
	[[nodiscard]] int32_t note_code() const { return note_code_after_arpeggiation_; }
	/// The note before the arpeggiator, or the MIDI channel it came on.
	[[nodiscard]] int32_t input_characteristic(MIDICharacteristic which) const {
		return input_characteristics_[util::to_underlying(which)];
	}
	/// A note-off came while note tails were not allowed, and was only remembered.
	[[nodiscard]] bool previously_ignored_note_off() const { return previously_ignored_note_off_; }
	/// Recomputes param @p param's final value from its preset alone (it has no cables), from @p control_block.
	void recalculate_param_with_no_cables(int32_t param, const matrix::MatrixBlock& control_block) {
		Evaluator::run_uncabled_param(matrix_pass(), control_block, param);
	}

	uint32_t get_local_lfo_phase_increment(const LFOConfig& config, deluge::modulation::params::Local param,
	                                       uint32_t synced_phase_increment);
	/// Renders one block, adding into @p args.buffer. @return false once the voice has ended and must be unassigned.
	bool render(const matrix::MatrixBlock& control_block, const VoiceConfig& cfg, const VoiceBlock& block,
	            const VoiceRenderArgs& args);

	/// Starts @p event's note. @p num_active_voices is the sound's voice count, which the cull order ranks by.
	/// @return false if the note cannot play and the voice must be unassigned again.
	bool note_on(const NoteSetup& setup, const SoundSnapshot& snapshot, const NoteOnEvent& event, VoiceClock clock,
	             size_t num_active_voices, dsp::fixed::Cong& noise);
	/// Moves the playing note to another, gliding when @p setup says to.
	void change_note(const NoteSetup& setup, const SoundSnapshot& snapshot, int32_t note_before_arpeggiation,
	                 int32_t note_after_arpeggiation, int32_t midi_channel,
	                 std::span<const int16_t, kNumExpressionDimensions> mpe);
	/// Each source's phase increment for the playing note, in each unison part; a source too high to render is made
	/// inactive.
	void calculate_phase_increments(const NoteSetup& setup, const SoundSnapshot& snapshot);
	/// Releases the note: each envelope from its sustain where it has one, or at once when there is no release stage.
	/// With note tails not allowed, the note-off is only remembered.
	void note_off(const ReleaseInfo& release, bool allow_release_stage, VoiceClock clock);
	void randomize_osc_phases(const SoundSnapshot& snapshot, dsp::fixed::Cong& noise);
	void expression_event_immediate(int32_t monophonic_value, int32_t voice_level_value, int32_t s);
	bool has_release_stage();
	/// The cull order's rating: higher is culled sooner. Ranks by @p voice_priority, then by @p num_active_voices (the
	/// sound's voice count), then by the amplitude envelope's stage, then by when it entered it.
	[[nodiscard]] uint32_t priority_rating(VoicePriority voice_priority, size_t num_active_voices) const;

	/// Stamp @ref envelope0_time_entered_ from @p clock if the amplitude envelope (envelopes_[0]) has just changed
	/// stage — call after any operation that may transition it.
	void stamp_envelope0_stage_entry(VoiceClock clock);
	/// Re-stamp while in sustain, to lower a drone note's cull likelihood.
	void reset_envelope0_time_entered(VoiceClock clock);

	void expression_event_smooth(int32_t new_value, int32_t s);

	/// Release immediately with provided release rate
	/// Returns whether voice should still be left active
	bool do_fast_release(uint32_t release_increment, VoiceClock clock);

	/// Sets envelope to off (will interpolate through this render window).
	/// Returns whether voice should still be left active
	bool do_immediate_release(VoiceClock clock);

	/// Already fading at (or faster than) the soft-cull rate. Cull victim selection and the voice limit
	/// must treat such a voice as already gone, or it soaks up culls meant for live voices (issue #4721).
	[[nodiscard]] bool is_cull_fading() const {
		return envelopes_[0].stage() >= Envelope::Stage::FAST_RELEASE
		       && envelopes_[0].fast_release_rate() >= kSoftCullIncrement;
	}

	bool speed_up_release(VoiceClock clock, uint32_t min_fast_release_increment = kSoftCullIncrement);

private:
	using Evaluator = matrix::Evaluator<matrix::MatrixFixed>;
	/// The voice's patching pass over its param plane: the local params from every source.
	[[nodiscard]] matrix::ScopePass matrix_pass() {
		return {.scope = GLOBALITY_LOCAL, .sources = params_.source_values, .final_values = params_.param_final_values};
	}

	// The per-voice modulation data plane: the *local* param final values (specific to this Voice) plus the patch
	// source values. At the start of source_values are local copies of the "global" sources — cheaper to copy them
	// here than to make the matrix pick and choose where to look.
	VoiceParams params_;

	std::bitset<kNumExpressionDimensions> expression_sources_currently_smoothing_;
	std::bitset<kNumExpressionDimensions> expression_sources_final_value_changed_;
	std::array<int32_t, kNumExpressionDimensions> local_expression_source_values_before_smoothing_{};

	std::array<Envelope, kNumEnvelopes> envelopes_{};

	// Voice-cull timestamp for the amplitude envelope (envelopes_[0]): the voice clock's tick when it entered its
	// stage. A voice-scheduling concern, not envelope DSP — the Envelope reports stage changes (take_stage_changed())
	// and the Voice stamps them. Read by priority_rating().
	uint32_t envelope0_time_entered_{};
	LFO lfo2_;
	LFO lfo4_;

	dsp::fixed::FilterSet filter_set_;

	// The note before the arpeggiator and the MIDI channel it came on, indexed by MIDICharacteristic
	std::array<int32_t, 2> input_characteristics_{};
	int32_t note_code_after_arpeggiation_{};

	uint32_t porta_envelope_pos_{};
	int32_t porta_envelope_max_amplitude_{};

	int32_t overall_osc_amplitude_last_time_{};

	bool done_first_render_{};
	bool previously_ignored_note_off_{};

	int32_t override_amplitude_envelope_release_rate_{};

	/// One of each engine; the sound's synth mode picks the one that plays.
	EngineSet engines_;
	SourceLevels source_levels_; ///< The source levels' de-zipper, which FM and the dual-source engines share
	/// The output stage's saturation; its memory starts at 0 and is set silent when the envelopes reset.
	deluge::effects::Saturation output_saturation_{0u};

	// The shared render scratch the render arguments carry, set at the top of render() for the render helpers. Each
	// entry is SSI_TX_BUFFER_NUM_SAMPLES wide.
	std::array<int32_t*, 4> spare_buffers_{};
	std::span<int32_t> sync_scratch_; ///< Set with the spare buffers at the top of render()
	/// The noise stream of the call in progress (render, note-on, phase randomizing), set at its top: the synth's one
	/// stream, which each call is handed.
	dsp::fixed::Cong* noise_{};

	[[nodiscard]] int32_t next_noise() { return static_cast<int32_t>(noise_->next()); }

	/// The controls a dual-source engine renders this block from, derived from the voice's params.
	[[nodiscard]] DualSourceControls dual_source_controls(const matrix::MatrixBlock& control_block,
	                                                      const VoiceConfig& cfg, const VoiceBlock& block,
	                                                      int32_t filter_gain, int32_t overall_pitch_adjust,
	                                                      bool first_render, bool stereo_unison) const;
	/// The controls FM renders this block from, derived from the voice's params.
	[[nodiscard]] FmControls fm_controls(int32_t overall_osc_amplitude, int32_t filter_gain,
	                                     int32_t overall_pitch_adjust, bool first_render, bool stereo_unison) const;
	/// The voice-level pan: whether to pan, and the gains (none when the sound pans, or renders the engine in mono).
	struct VoicePan {
		bool do_panning{};
		int32_t amplitude_l{};
		int32_t amplitude_r{};
	};
	/// What the output stage writes from: the voice's filtered buffer, into the sound's.
	struct VoiceOutput {
		const int32_t* buffer{}; ///< The voice's buffer, stereo pairs when the engine rendered stereo
		int32_t num_samples{};
		int32_t* sound_buffer{};       ///< The sound's buffer, stereo pairs when @ref sound_in_stereo
		int32_t amplitude_start{};     ///< The overall amplitude at block start (q31)
		int32_t amplitude_increment{}; ///< Its per-sample increment
		bool fm{};                     ///< FM worked the overall amplitude into its carriers already
		VoicePan pan;
		bool sound_in_stereo{};
		uint8_t clipping_amount{};
	};

	/// The envelopes, local LFOs, MPE smoothing and patching for this block. @return whether the amplitude envelope
	/// has ended, so the voice is done after this block.
	bool render_modulation(const matrix::MatrixBlock& control_block, const VoiceConfig& cfg, const VoiceBlock& block,
	                       int32_t num_samples, uint32_t sources_changed);
	/// Moves the MPE sources that are smoothing towards their targets for this block.
	void smooth_expression(const VoiceConfig& cfg, int32_t num_samples);
	/// The sources a play-once sample's auto-release would follow: each active one, when every active one is a
	/// play-once sample that may release itself; else none.
	[[nodiscard]] static uint32_t play_once_sample_sources(const VoiceConfig& cfg, const VoiceBlock& block);
	/// Every unison part's sources ended. @return true to unassign at once; with filters on, it fast-releases instead.
	[[nodiscard]] bool end_with_parts();
	/// The voice's pitch adjust this block: the patched one, the kit's, bend and MPE, and portamento (which advances).
	int32_t pitch_adjust(const VoiceConfig& cfg, int32_t num_samples, int32_t external_pitch_adjust);
	[[nodiscard]] VoicePan voice_pan(const VoiceConfig& cfg, bool applying_pan_at_voice_level) const;
	/// Adds the voice's stereo buffer into the sound's, with the overall amplitude, saturation and pan.
	template <bool kClipping>
	void write_stereo(const VoiceOutput& output);
	/// Adds the voice's mono buffer into the sound's (panned to stereo when the sound renders stereo).
	template <bool kClipping>
	void write_mono(const VoiceOutput& output);
	/// Each FM modulator's volume param this block.
	[[nodiscard]] std::array<int32_t, kNumModulators> modulator_volumes() const {
		return {params_.param_final_values[deluge::modulation::params::LOCAL_MODULATOR_0_VOLUME],
		        params_.param_final_values[deluge::modulation::params::LOCAL_MODULATOR_1_VOLUME]};
	}
	/// Whether a source that can change during the note modulates source @p s's pitch, so its sample is not cached.
	[[nodiscard]] bool pitch_mod_disallows_cache(const matrix::MatrixBlock& control_block, const VoiceConfig& cfg,
	                                             int32_t s) const;
	void setup_porta(int32_t last_note_code);
	/// Starts the release of a voice of play-once samples so that it ends with them.
	void auto_release_if_due(const VoiceConfig& cfg, const VoiceBlock& block, int32_t overall_pitch_adjust);
	[[nodiscard]] int32_t combine_expression_values(int32_t synth_level_value, int32_t expression_dimension) const;
};

} // namespace deluge::synth
