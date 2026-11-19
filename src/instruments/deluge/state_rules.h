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

/// @file
/// @brief The rules a change to a sound's state brings with it: what else changes when the engine, an oscillator's
/// type or a sample's loop mode does.

#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"

#include <span>

namespace deluge::instrument {

/// @brief @p state with its engine set to @p mode. Into FM both filters go off; out of it a filter that is off comes
/// back on (the low-pass as the 24 dB transistor ladder, the high-pass as the ladder).
[[nodiscard]] FlatSoundState with_mode(FlatSoundState state, SynthMode mode);

/// @brief @p state with slot @p id's oscillator set to @p type. When the ranges change kind (sample to wavetable or
/// back) the record's ranges keep only their top notes, and a slot left with none gains one open range; otherwise the
/// slot takes the ranges it holds now. A DX7 slot without a patch gains the init voice.
/// @param held The ranges the slot plays now (the runtime holds them until it holds no ranges of its own).
/// @param held_are_wavetable Whether those ranges are a wavetable's.
[[nodiscard]] FlatSoundState with_osc_type(FlatSoundState state, SourceId id, OscType type,
                                           std::span<const RangeState> held, bool held_are_wavetable);

/// @brief @p state with slot @p id's loop mode set: stretching to note length makes pitch and speed independent, and
/// leaving it gives that up.
[[nodiscard]] FlatSoundState with_loop_mode(FlatSoundState state, SourceId id, SampleRepeatMode mode);

/// @brief @p state with the effect settings that follow the song set from @p defaults: a new sound is
/// `with_song_defaults(FlatSoundState{}, defaults)`.
[[nodiscard]] FlatSoundState with_song_defaults(FlatSoundState state, const effects::SongDefaults& defaults);

/// @brief @p state set up to play samples (a new kit row, the sample browser's preview): polyphony automatic, the 24dB
/// ladder low-pass, both oscillators sample ones.
[[nodiscard]] FlatSoundState as_sample_sound(FlatSoundState state);

/// @brief @p state set up as the default new synth: a saw an octave down over a square, four detuned unison parts,
/// the 24dB ladder low-pass.
[[nodiscard]] FlatSoundState as_default_synth(FlatSoundState state);

/// @brief @p state set up as a blank new synth, a DX7 one when @p dx (its first oscillator the init voice).
[[nodiscard]] FlatSoundState as_blank_synth(FlatSoundState state, bool dx);

/// @brief @p state with its engine set to @p mode, as with_mode() sets it: what leaves is kept for a switch back, what
/// arrives comes from what was kept (an FM switch carries each slot's pitch to its carrier and back).
[[nodiscard]] SoundState switch_engine(SoundState state, SynthMode mode);

/// @brief @p state with slot @p id's oscillator set to @p type, as with_osc_type() sets it; what the new kind does not
/// hold is kept for a switch back.
[[nodiscard]] SoundState switch_source(SoundState state, SourceId id, OscType type, std::span<const RangeState> held,
                                       bool held_are_wavetable);

/// @brief with_loop_mode() on a sound's record.
[[nodiscard]] SoundState with_loop_mode(SoundState state, SourceId id, SampleRepeatMode mode);

/// @brief with_song_defaults() on a sound's record.
[[nodiscard]] SoundState with_song_defaults(SoundState state, const effects::SongDefaults& defaults);

/// @brief as_sample_sound() on a sound's record.
[[nodiscard]] SoundState as_sample_sound(SoundState state);

/// @brief as_default_synth() on a sound's record.
[[nodiscard]] SoundState as_default_synth(SoundState state);

/// @brief as_blank_synth() on a sound's record.
[[nodiscard]] SoundState as_blank_synth(SoundState state, bool dx);

} // namespace deluge::instrument
