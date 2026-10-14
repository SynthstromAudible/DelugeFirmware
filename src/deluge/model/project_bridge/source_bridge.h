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

#include "definitions_cxx.hpp" // Error
#include "engines/deluge/sound_state.h"

#include <cstdint>
#include <optional>
#include <vector>

class Sound;
class Source;

namespace deluge::bridge {

/// @brief Source @p source's ranges as the record holds them: what the runtime plays.
///
/// @param source The runtime source.
/// @return The source's ranges.
[[nodiscard]] std::vector<deluge::instrument::RangeState> rangesOf(const ::Source& source);

/// @brief Make source @p s of @p sound play @p ranges, changing ranges in place: top notes, a sample range's zone,
/// loop points, transpose and cents (kept by sample ranges whatever the source's type). When they equal the source's
/// own, only the record is reseeded: a gesture that loaded files or reshaped the list itself records its result this
/// way. Kills no voices: a zone moves under a sounding note, as the marker editor moves it.
///
/// @param sound  The sound.
/// @param s      The source index.
/// @param ranges The ranges to play.
/// @return Error::UNSPECIFIED when the list has another length or another file in any range (assigning a file is its
/// gesture's own load), or a top note it moves does not rise between its neighbours.
Error applyRanges(::Sound& sound, int32_t s, const std::vector<deluge::instrument::RangeState>& ranges);

/// @brief Source @p source's DX7 patch as the record holds it, or nullopt when it has none.
///
/// @param source The runtime source.
/// @return The patch, or nullopt.
[[nodiscard]] std::optional<deluge::instrument::DxPatchState> dxPatchOf(const ::Source& source);

/// @brief Give source @p s of @p sound the patch @p patch: its 156 voice bytes, its engine mode and its random detune.
/// Its voices take the new patch at their next note, as a menu edit does.
///
/// @param sound The sound.
/// @param s     The source index.
/// @param patch The patch to give.
/// @return Error::UNSPECIFIED unless the source is a DX7 oscillator.
Error applyDxPatch(::Sound& sound, int32_t s, const deluge::instrument::DxPatchState& patch);

} // namespace deluge::bridge
