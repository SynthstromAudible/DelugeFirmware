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

#include <cstdint>

namespace deluge::storage {

/// @brief How many bytes of a song file the preview decoder is given.
///
/// Chosen from measurement, not margin-guessing: across the whole song corpus the `preview` attribute
/// begins at byte 114 -- it sits on the root `<song>` element, immediately after `previewNumPads` --
/// and its payload is ~864 hex characters (144 pads x 3 colours x 2). The JSON writer was measured
/// separately against `tests/data/songs/JsonPreview.Json`, since the corpus carries no JSON song.
///
/// A song whose preview falls outside this window renders BLANK, indistinguishably from a song that
/// has no preview at all. That is why `song_preview_window_spec.cpp` asserts the bound over the whole
/// corpus rather than trusting this number: if a future writer moves the attribute later, CI says so
/// instead of artwork silently disappearing.
constexpr uint32_t kSongPreviewWindow = 4096;

} // namespace deluge::storage
