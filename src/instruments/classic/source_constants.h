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

/// @file
/// @brief The source values the record and the file share: a range's open top note and unset end, the free-running
/// phase word, and the DX7 patch as the record names it.

#pragma once

#include "synth/sources/source_state.h"

#include <cstdint>

namespace deluge::instruments::classic {

/// @brief The top note of a source's last range, when a file names none: it covers every note above the range before
/// it.
inline constexpr int16_t kOpenTopNote = 32767;

/// @brief The phase word of an oscillator that runs free: the file writes it and the render adds it.
using synth::kFreeRunningPhase;

/// @brief The end a range's zone has before a file sets it: a file loaded into it plays to its own end.
inline constexpr uint64_t kUnsetEndPos = 9999999;

/// @brief The DX7 patch the DX7 engine plays: the 156 voice bytes and two extensions.
using DxPatchState = synth::DxPatch;

} // namespace deluge::instruments::classic
