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
#include <vector>

class Sound;
class Source;

namespace deluge::bridge {

/// @brief Source @p source's ranges as the record holds them: what the runtime plays now.
[[nodiscard]] std::vector<deluge::instrument::RangeState> rangesOf(const ::Source& source);

/// @brief Make source @p s of @p sound play @p ranges, changing ranges in place: top notes, a sample range's zone,
/// loop points, transpose and cents. Nothing when they equal the source's own. Kills no voices: a zone moves under a
/// sounding note, as the marker editor has always moved it.
/// @return Error::UNSPECIFIED when the list has another length or another file in any range (assigning a file is its
/// gesture's own load), or its top notes do not rise.
Error applyRanges(::Sound& sound, int32_t s, const std::vector<deluge::instrument::RangeState>& ranges);

} // namespace deluge::bridge
