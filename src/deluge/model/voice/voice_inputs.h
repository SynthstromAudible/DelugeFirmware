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

#include "definitions_cxx.hpp" // kNumSources, OscType, kNumExpressionDimensions, kFirstLocalSource
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "synth/modulation/control_block.h"
#include "util/misc.h" // util::to_underlying
#include "util/phase_increment_fine_tuner.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace deluge::synth {

/// @brief The voice-state clock the cull order reads: each envelope stage a voice enters takes the next tick, so of two
/// voices in one stage the one that entered it first is culled first.
struct VoiceClock {
	uint32_t& next; ///< The next tick to hand out, shared by every voice.

	/// @return The next tick, advancing the clock.
	[[nodiscard]] uint32_t tick() const { return next++; }
};

/// @brief What one voice of a sound renders from, beyond its sound's config: what differs between the sound's voices,
/// or changes from one voice's render to the next.
struct VoiceBlock {
	/// Each WAVETABLE source's band data (empty for any other source, or a source with no table loaded).
	std::array<dsp::fixed::WaveTableData, kNumSources> wave_tables{};
	/// Per source: a play-once sample may release itself before its end. False when no sample is playing, or while its
	/// loop end is still obeyed (until the note-off).
	std::array<bool, kNumSources> sample_can_auto_release{};
	/// Per source: it renders in stereo (the engine renders in stereo, and the source spreads its unison or plays a
	/// stereo sample or input).
	std::array<bool, kNumSources> source_in_stereo{};
	size_t num_active_voices{}; ///< The sound's voice count, which the cull order ranks by.
	VoiceClock clock;
};

/// @brief One voice render's buffer and the sound-level scalars that go with it.
struct VoiceRenderArgs {
	int32_t* buffer{};      ///< The sound's buffer, which the voice adds into (stereo pairs when @ref sound_in_stereo)
	int32_t num_samples{};  ///< Frames to render
	bool sound_in_stereo{}; ///< The sound renders its voices in stereo
	bool pan_at_voice_level{};  ///< Each voice pans itself (pan is patched), rather than the sound panning the sum
	uint32_t sources_changed{}; ///< Bit per patch source whose value changed since the last block
	int32_t pitch_adjust{};     ///< The sound's pitch adjust, 2^24 for none
	/// Shared scratch, each SSI_TX_BUFFER_NUM_SAMPLES wide: the render's stereo, ring-mod and osc-sync work buffers.
	std::array<int32_t*, 4> spare_buffers{};
};

/// A NoteSetup's @ref NoteSetup::last_note_code when the sound has played no note yet.
inline constexpr int32_t kNoLastNote = std::numeric_limits<int32_t>::min();

/// @brief One source as a note starts or changes.
struct SourceSetup {
	OscType osc_type{OscType::SINE}; ///< The source's type as set; FM renders sines whatever it is
	bool ever_active{};              ///< The source can sound at all at the sound's current settings
	/// A SAMPLE or WAVETABLE source found a range holding a loaded file for the note. Always true for other sources.
	bool playable{true};
	int32_t transpose{};                  ///< Semitones: the playing sample's own, else the source's
	PhaseIncrementFineTuner fine_tuner{}; ///< Cents: the playing sample's own, else the source's
	/// The phase increment that plays the source at its root pitch: the playing sample's, else unity.
	int32_t neutral_phase_increment{kMaxSampleValue};
	void* dx_patch{}; ///< A DX7 source's patch, the handle its port takes
};

/// @brief What a note-on or a note change needs from its sound, beyond the sound's snapshot.
struct NoteSetup {
	std::array<SourceSetup, kNumSources> sources{};
	int32_t last_note_code{kNoLastNote}; ///< The sound's previous note, which portamento glides from
	bool porta{};                        ///< Glide from @ref last_note_code
	std::array<int32_t, kNumExpressionDimensions> monophonic_expression{}; ///< The sound's own MPE values
	/// The sound's global source values, which the initial patching starts the voice's copies from.
	std::array<int32_t, util::to_underlying(kFirstLocalSource)> global_source_values{};
	std::array<bool, kNumEnvelopes> attack_is_zero{};     ///< Per envelope: its attack is at its minimum
	std::array<bool, kNumModulators> modulator_audible{}; ///< Per FM modulator: its volume is not silent
	ControlBlock initial_patching{}; ///< The block the initial patching reads: every param's preset
};

/// @brief One note-on as it reaches a voice.
struct NoteOnEvent {
	int32_t note_before_arp{};
	int32_t note_after_arp{};
	uint8_t velocity{};
	uint32_t samples_late{}; ///< How far into the note playback starts, for a note started late
	bool reset_envelopes{};  ///< A new voice; false when a voice is re-triggered and continues from where it was
	int32_t midi_channel{};
	std::span<const int16_t, kNumExpressionDimensions> mpe; ///< The note's MPE values, Q15
};

/// @brief What a note-off needs from its sound.
struct ReleaseInfo {
	/// Per envelope: it has a sustain at the sound's current params, so it releases from its sustain.
	std::array<bool, kNumEnvelopes> envelope_has_sustain{};
	bool note_tails_allowed{};                    ///< When false, the note-off is only remembered
	std::array<OscType, kNumSources> osc_types{}; ///< Each source's type as set
	SynthMode synth_mode{SynthMode::SUBTRACTIVE};
	uint8_t num_unison{1};
	VoicePriority voice_priority{VoicePriority::MEDIUM}; ///< With @ref num_active_voices, ranks the voice to cull
	size_t num_active_voices{};                          ///< The sound's voice count
};

} // namespace deluge::synth
