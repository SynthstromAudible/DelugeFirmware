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

#include "synth/sources/source_state.h"

#include <cstdint>
#include <string>

namespace deluge::instrument {

/// @brief The top note of a source's last range, when a file names none: it covers every note above the range before
/// it.
inline constexpr int16_t kOpenTopNote = 32767;

using synth::kFreeRunningPhase;

/// @brief The end a range's zone has before a file sets it: a file loaded into it plays to its own end.
inline constexpr uint64_t kUnsetEndPos = 9999999;

/// @brief The DX7 patch a DX7 source carries: the 156 voice bytes and two extensions.
using DxPatchState = synth::DxPatch;

} // namespace deluge::instrument
