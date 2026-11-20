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

#include "instruments/deluge/range_state.h"

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

/// @brief A sound's settings in the file's flat layout: every engine's and every source kind's fields side by side, as
/// the codec reads and writes them. The record (SoundState) composes it into what the sound plays and what it keeps. A
/// default is a new synth's, but for the two effect settings that follow the song (with_song_defaults).
struct FlatSoundState {
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
	std::array<uint32_t, 2> osc_retrigger_phase{kFreeRunningPhase, kFreeRunningPhase};
	std::array<uint32_t, 2> modulator_retrigger_phase{}; ///< kFreeRunningPhase where a modulator runs free
	std::array<LFOType, 4> lfo_wave_type{LFOType::TRIANGLE, LFOType::TRIANGLE, LFOType::TRIANGLE, LFOType::TRIANGLE};
	std::array<plugin::SyncType, 4> lfo_sync_type{};
	std::array<plugin::SyncLevel, 4> lfo_sync_level{};
	deluge::effects::EffectStates effects; ///< the sound's own effects' settings
	std::array<SourceState, 2> sources;
	bool operator==(const FlatSoundState&) const = default;
};

} // namespace deluge::instrument
