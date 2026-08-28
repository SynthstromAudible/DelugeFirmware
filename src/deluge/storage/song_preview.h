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
/// Chosen from measurement, not margin-guessing, on the XML arm at least. Across the whole song
/// corpus (42 files) the `preview` attribute sits on the root `<song>` element, immediately after
/// `previewNumPads` -- but NOT at one fixed offset: 30 files put it at byte 114, and 10 put it at
/// byte 119, the difference tracking the length of the `firmwareVersion` string that precedes it
/// (an 11-char version like "4.1.4-alpha" pushes it 5 bytes later than a 6-char one like "c1.3.0").
/// Its payload is ~864 hex characters (144 pads x 3 colours x 2); the largest corpus payload measured
/// ends at byte 1014, still well inside this window.
///
/// The JSON arm is NOT measured the same way -- the corpus contains zero real JSON songs, so there is
/// nothing to measure. `tests/data/songs/JsonPreview.Json` is a hand-traced fixture, built by walking
/// `JsonSerializer`'s actual write calls in `Song::writeToFile()` byte-for-byte rather than by
/// capturing real writer output. It is the ONLY thing pinning the JSON arm of this constant: it can
/// catch a future change to that call sequence, but it cannot catch a divergence between this trace
/// and what the real firmware binary emits, because no such binary output has ever been captured.
/// Anyone re-deriving this constant (including a Rust-side `static_assert` against it) should treat
/// the JSON side as unverified against a real writer until a JSON-producing harness exists and a
/// captured fixture replaces this one.
///
/// A song whose preview falls outside this window renders BLANK, indistinguishably from a song that
/// has no preview at all. That is why `song_preview_window_tests.cpp` asserts the bound over the
/// whole corpus rather than trusting this number: if a future writer moves the attribute later, CI
/// says so instead of artwork silently disappearing.
constexpr uint32_t kSongPreviewWindow = 4096;

} // namespace deluge::storage
