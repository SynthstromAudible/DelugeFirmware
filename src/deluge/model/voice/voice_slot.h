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

#include "definitions_cxx.hpp"
#include "model/voice/sample_playback_adapter.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "processing/dx7/dx_voice_adapter.h"
#include "processing/live/audio_input_adapter.h"
#include "synth/voice/voice.h"
#include <array>
#include <compare>
#include <cstdint>
#include <span>

class ModelStackWithSoundFlags;
class ParamManagerForTimeline;
class Sound;

/// @brief The firmware side of one unison part's source: the ports its sources play through.
struct UnisonPartPorts {
	SamplePlaybackAdapter sample;   ///< the VoiceSample pool and reader behind synth::SamplePlaybackPort
	deluge::dsp::DxVoiceAdapter dx; ///< the pooled dx7::Voice behind synth::DxVoicePort
	AudioInputAdapter input;        ///< the AudioEngine-backed live input behind synth::AudioInputPort
};

/// @brief A sound's voice as the app holds it: one synth::Voice with the ports its sources play through, the playback
/// guides of the files its note plays, and the sound it belongs to. Builds what the kernel's lifecycle reads from the
/// sound (voice_setup.h) and hands it over.
class VoiceSlot final {
public:
	explicit VoiceSlot(Sound& sound);
	VoiceSlot(const VoiceSlot&) = delete;
	VoiceSlot& operator=(const VoiceSlot&) = delete;
	VoiceSlot(VoiceSlot&&) = delete;
	VoiceSlot& operator=(VoiceSlot&&) = delete;
	~VoiceSlot() { set_as_unassigned(nullptr); }

private:
	// Declared before the kernel, whose sources point into it, so it outlives them.
	std::array<std::array<UnisonPartPorts, kNumSources>, kMaxNumVoicesUnison> ports_;

public:
	deluge::synth::Voice kernel;
	Sound& sound;
	/// Each source's playback bounds and file for the note playing (basically sample memory bounds).
	std::array<VoiceSamplePlaybackGuide, kNumSources> guides{};
	uint32_t order_sounded{}; ///< When the voice's note started, counted across every sound's voices
	/// The voice's first render call only marks it created: starting a voice costs more than rendering one, so the work
	/// is spread over two render blocks.
	bool just_created{false};

	/// The ports unison part @p u's source @p s plays through.
	[[nodiscard]] UnisonPartPorts& ports(int32_t u, int32_t s) { return ports_[u][s]; }

	/// The voice's rating in the cull order (higher is culled sooner), for its sound as it is now.
	[[nodiscard]] uint32_t priority_rating() const;
	[[nodiscard]] std::strong_ordering operator<=>(const VoiceSlot& other) const {
		return priority_rating() <=> other.priority_rating();
	}

	// --- The lifecycle, each building what the kernel reads from the sound
	bool note_on(ModelStackWithSoundFlags& flags, const deluge::synth::NoteOnEvent& event, uint32_t sample_sync_length,
	             int32_t ticks_late);
	void change_note(ModelStackWithSoundFlags& flags, int32_t note_before_arp, int32_t note_after_arp,
	                 int32_t midi_channel, std::span<const int16_t, kNumExpressionDimensions> mpe);
	void calculate_phase_increments(ModelStackWithSoundFlags& flags);
	void note_off(ParamManagerForTimeline* paramManager, bool note_tails_allowed, bool allow_release_stage = true);
	void randomize_osc_phases();
	void expression_event_immediate(int32_t voice_level_value, int32_t s);
	/// A sample's zone moved under the playing note. @return false if the voice must be unassigned now.
	bool sample_zone_changed(ModelStackWithSoundFlags& flags, int32_t s, MarkerType marker);
	/// Renders one block of the voice. @return false once it has ended and must be unassigned.
	bool render(const deluge::synth::matrix::MatrixBlock& control_block, const deluge::synth::VoiceConfig& cfg,
	            const deluge::synth::VoiceRenderArgs& args);

	// --- Releases, stamped by the engine's voice clock. Each returns whether the voice still sounds.
	bool do_fast_release(uint32_t release_increment);
	bool speed_up_release();
	bool do_immediate_release();

	/// Unassigns the voice from its sound (@p flags may be null, as when every voice is unassigned on a song swap).
	void set_as_unassigned(ModelStackWithSoundFlags* flags, bool deleting_song = false);
	/// Releases each unison part's sources.
	void unassign_stuff(bool deleting_song);
	[[nodiscard]] bool should_be_deleted() const { return delete_this_voice_; }

private:
	bool delete_this_voice_{false};

	/// What differs between the sound's voices for this voice's render.
	[[nodiscard]] deluge::synth::VoiceBlock block() const;
};
