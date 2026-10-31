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
#include "synth/modulation/envelope.h"
#include "synth/modulation/lfo.h"
#include "synth/modulation/param_plane.h"
#include "synth/modulation/patcher.h"
#include "synth/voice/fm_engine.h"
#include "synth/voice/ringmod_engine.h"
#include "synth/voice/sound_snapshot.h"
#include "synth/voice/source_component_render.h" // deluge::synth::SourceRenderResult
#include "synth/voice/subtractive_engine.h"
#include "synth/voice/voice_config.h"
#include "synth/voice/voice_inputs.h"
#include "synth/voice/voice_unison_part.h"
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
	Patcher patcher;

	// Stores all oscillator positions and stuff, for each Source within each Unison too
	std::array<VoiceUnisonPart, kMaxNumVoicesUnison> unison_parts{};

	// The per-voice modulation data plane: the *local* param final values (specific to this Voice) plus the patch
	// source values. At the start of source_values are local copies of the "global" sources — cheaper to copy them
	// here than to make the Patcher pick and choose where to look.
	VoiceParams params;

	std::bitset<kNumExpressionDimensions> expression_sources_currently_smoothing{};
	std::bitset<kNumExpressionDimensions> expression_sources_final_value_changed{};
	std::array<int32_t, kNumExpressionDimensions> local_expression_source_values_before_smoothing{};

	std::array<Envelope, kNumEnvelopes> envelopes{};

	// Voice-cull timestamp for the amplitude envelope (envelopes[0]): the voice clock's tick when it entered its
	// stage. A voice-scheduling concern, not envelope DSP — the Envelope reports stage changes (take_stage_changed())
	// and the Voice stamps them. Read by priority_rating().
	uint32_t envelope0_time_entered{};
	LFO lfo2;
	LFO lfo4;

	dsp::fixed::FilterSet filter_set;

	// The ring-mod synth engine: owns the cross-source ring-mod relationship (renders both sources + combines). It
	// holds no state: the VA and wavetable components are owned per source on the unison parts.
	RingModEngine ring_mod_engine;

	// The subtractive synth engine: owns the per-source render loop, osc sync and the mono-to-stereo dance; each
	// source's render it hands back to render_basic_source.
	SubtractiveEngine subtractive_engine;

	// The FM synth engine: owns one unison part's FM topology (modulators, carriers, feedback); it reads the voice's
	// params and unison parts through the Voice& it is handed, and calls adjust_pitch.
	FMEngine fm_engine;

	// The note before the arpeggiator and the MIDI channel it came on, indexed by MIDICharacteristic
	std::array<int32_t, 2> input_characteristics{};
	int32_t note_code_after_arpeggiation{};

	uint32_t porta_envelope_pos{};
	int32_t porta_envelope_max_amplitude{};

	int32_t overall_osc_amplitude_last_time{};
	std::array<int32_t, kNumSources> source_amplitudes_last_time{};
	std::array<uint32_t, kNumSources> source_wave_indexes_last_time{};

	int32_t filter_gain_last_time{};
	bool done_first_render{};
	bool previously_ignored_note_off{};

	int32_t override_amplitude_envelope_release_rate{};

	uint32_t get_local_lfo_phase_increment(const LFOConfig& config, deluge::modulation::params::Local param,
	                                       uint32_t synced_phase_increment);
	/// Scale @p phase_increment by @p adjustment (a pitch-adjust patch value); @return false if it overflows (the
	/// note frequency is too high and the source/modulator should be dropped). Pure — used by Voice's per-source
	/// pitch math and by FMEngine for its modulators.
	static bool adjust_pitch(uint32_t& phase_increment, int32_t adjustment);
	/// Renders one block, adding into @p args.buffer. @return false once the voice has ended and must be unassigned.
	bool render(const ControlBlock& control_block, const VoiceConfig& cfg, const VoiceBlock& block,
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

	/// Stamp @ref envelope0_time_entered from @p clock if the amplitude envelope (envelopes[0]) has just changed stage
	/// — call after any operation that may transition it.
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
		return envelopes[0].stage() >= Envelope::Stage::FAST_RELEASE
		       && envelopes[0].fast_release_rate() >= kSoftCullIncrement;
	}

	bool speed_up_release(VoiceClock clock, uint32_t min_fast_release_increment = kSoftCullIncrement);

	// The per-source render driver and the unison-inactive predicate, public for SubtractiveEngine to drive.
	/// Renders source @p s in each unison part. @p phase_increments_out, unless empty, takes each part's increment (0
	/// when too high to render) for the parts osc sync follows. @return whether a unison part became inactive (its
	/// sample ended), which the caller collects across sources.
	[[nodiscard]] bool render_basic_source(const ControlBlock& control_block, const VoiceBlock& block, int32_t s,
	                                       int32_t* osc_buffer, int32_t num_samples, bool stereo_buffer,
	                                       int32_t source_amplitude, int32_t overall_pitch_adjust, bool do_osc_sync,
	                                       uint32_t* osc_sync_pos, uint32_t* osc_sync_phase_increments,
	                                       int32_t amplitude_increment, std::span<uint32_t> phase_increments_out,
	                                       bool get_out_after_phase_increments, int32_t wave_index_increment,
	                                       const VoiceConfig& cfg);
	[[nodiscard]] bool are_all_unison_parts_inactive(const VoiceConfig& cfg) const;

private:
	/// The output stage's saturation; its memory starts at 0 and is set silent when the envelopes reset.
	deluge::effects::Saturation output_saturation_{0u};

	// The shared render scratch the render arguments carry, set at the top of render() for the render helpers. Each
	// entry is SSI_TX_BUFFER_NUM_SAMPLES wide.
	std::array<int32_t*, 4> spare_buffers_{};
	std::span<int32_t> sync_scratch_{}; ///< Set with the spare buffers at the top of render()
	/// The noise stream of the call in progress (render, note-on, phase randomizing), set at its top: the synth's one
	/// stream, which each call is handed.
	dsp::fixed::Cong* noise_{};

	[[nodiscard]] int32_t next_noise() { return static_cast<int32_t>(noise_->next()); }

	// Render one unison part of one source, dispatching by its type to the per-source component (sample, input, DX7,
	// virtual-analog, wavetable or supersaw) and folding their outcomes into one result. Called from
	// render_basic_source's per-unison loop after the shared pitch, pan and sync setup.
	deluge::synth::SourceRenderResult render_source_component(
	    int32_t u, int32_t s, const ControlBlock& control_block, int32_t* osc_buffer, int32_t num_samples,
	    bool stereo_buffer, int32_t source_amplitude, int32_t amplitude_increment, uint32_t phase_increment,
	    bool do_osc_sync, uint32_t* osc_sync_pos, uint32_t* osc_sync_phase_increments, int32_t wave_index_increment,
	    bool stereo_unison, int32_t amplitude_l, int32_t amplitude_r, const VoiceConfig& cfg, const VoiceBlock& block);
	void setup_porta(int32_t last_note_code);
	/// Starts the release of a voice of play-once samples so that it ends with them.
	void auto_release_if_due(const VoiceConfig& cfg, const VoiceBlock& block, int32_t overall_pitch_adjust);
	int32_t combine_expression_values(int32_t synth_level_value, int32_t expression_dimension) const;
};

} // namespace deluge::synth
