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
