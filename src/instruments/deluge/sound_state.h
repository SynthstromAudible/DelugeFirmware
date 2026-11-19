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

#include "effects/state.h"
#include "instruments/deluge/words.h"
#include "plugin-api/types/interpolation_mode.h"
#include "plugin-api/types/sync.h"
#include "plugin-api/types/voice_priority.h"
#include "synth/sources/source_state.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deluge::instrument {

/// @brief The top note of a source's last range, when a file names none: it covers every note above the range before
/// it.
inline constexpr int16_t kOpenTopNote = 32767;

/// @brief The end a range's zone has before a file sets it: a file loaded into it plays to its own end.
inline constexpr uint64_t kUnsetEndPos = 9999999;

/// @brief One sample or wavetable key-split range of a source, ordered by `top_note` (the highest MIDI note it covers).
/// A wavetable range uses only `top_note` and `file_path`.
struct RangeState {
	int16_t top_note{0};
	std::string file_path;      ///< the sample's path on the card
	uint64_t start_pos{0};      ///< in samples
	uint64_t end_pos{0};        ///< in samples
	uint32_t loop_start_pos{0}; ///< in samples; 0 = none
	uint32_t loop_end_pos{0};   ///< in samples; 0 = none
	int16_t transpose{0};       ///< semitones
	int8_t cents{0};
	bool operator==(const RangeState&) const = default;
};

/// @brief The DX7 patch a DX7 source carries: the 156 voice bytes and two extensions.
using DxPatchState = synth::DxPatch;

/// @brief One of a sound's two sources.
struct SourceState {
	OscType osc_type{OscType::SQUARE};
	int16_t transpose{0}; ///< semitones; non-sample types only
	int8_t cents{0};      ///< non-sample types only
	SampleRepeatMode repeat_mode{SampleRepeatMode::CUT};
	int8_t time_stretch_amount{0};
	plugin::InterpolationMode interpolation_mode{plugin::InterpolationMode::SMOOTH};
	bool pitch_and_speed_independent{false};
	bool reversed{false};
	std::optional<DxPatchState> dx_patch; ///< present iff the source has a DX7 patch
	std::vector<RangeState> ranges;       ///< ordered by top_note
	bool operator==(const SourceState&) const = default;
};

/// @brief The Deluge instrument's settings for one sound: everything about it that is not a param. A default state is
/// a new synth's, but for the two effect settings that follow the song (with_song_defaults).
struct SoundState {
	plugin::VoicePriority voice_priority{plugin::VoicePriority::MEDIUM};
	uint8_t max_voice_count{8};
	int32_t side_chain_send_level{0};
	uint8_t output_midi_channel{255};       ///< 255 = none
	uint8_t output_midi_note_for_drum{255}; ///< 255 = none
	bool oscillator_sync{false};
	PolyphonyMode polyphonic{PolyphonyMode::POLY};
	SynthMode synth_mode{SynthMode::SUBTRACTIVE};
	int16_t transpose{0}; ///< semitones
	int8_t cents{0};      ///< master fine tune, −50..50
	uint8_t num_unison{1};
	int8_t unison_detune{8};
	uint8_t unison_stereo_spread{0};
	bool modulator1_to_modulator0{false};
	std::array<int16_t, 2> modulator_transpose{0, -12};
	std::array<int8_t, 2> modulator_cents{};
	std::array<uint32_t, 2> osc_retrigger_phase{0xFFFFFFFF, 0xFFFFFFFF}; ///< 0xFFFFFFFF = free-running
	std::array<uint32_t, 2> modulator_retrigger_phase{};                 ///< 0xFFFFFFFF = free-running
	std::array<LFOType, 4> lfo_wave_type{LFOType::TRIANGLE, LFOType::TRIANGLE, LFOType::TRIANGLE, LFOType::TRIANGLE};
	std::array<plugin::SyncType, 4> lfo_sync_type{};
	std::array<plugin::SyncLevel, 4> lfo_sync_level{};
	deluge::effects::EffectStates effects; ///< the sound's own effects' settings
	std::array<SourceState, 2> sources;
	bool operator==(const SoundState&) const = default;
};

} // namespace deluge::instrument
