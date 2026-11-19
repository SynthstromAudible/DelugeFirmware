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
[[nodiscard]] SoundState with_mode(SoundState state, SynthMode mode);

/// @brief @p state with slot @p id's oscillator set to @p type. When the ranges change kind (sample to wavetable or
/// back) the record's ranges keep only their top notes, and a slot left with none gains one open range; otherwise the
/// slot takes the ranges it holds now. A DX7 slot without a patch gains the init voice.
/// @param held The ranges the slot plays now (the runtime holds them until it holds no ranges of its own).
/// @param held_are_wavetable Whether those ranges are a wavetable's.
[[nodiscard]] SoundState with_osc_type(SoundState state, SourceId id, OscType type, std::span<const RangeState> held,
                                       bool held_are_wavetable);

/// @brief @p state with slot @p id's loop mode set: stretching to note length makes pitch and speed independent, and
/// leaving it gives that up.
[[nodiscard]] SoundState with_loop_mode(SoundState state, SourceId id, SampleRepeatMode mode);

} // namespace deluge::instrument
