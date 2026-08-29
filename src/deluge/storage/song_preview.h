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

#include "board_layout.hpp"
#include "definitions_cxx.hpp"
#include "gui/colour/rgb.h"

#include <cstdint>
#include <span>

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

/// @brief What a preview decode found.
enum class SongPreviewOutcome {
	/// A `preview` attribute was found and its pads were written to the caller's image.
	Rendered,
	/// The document was walked to its end and simply has no `preview` attribute. The image is
	/// untouched, and blank artwork is the CORRECT answer for this song.
	NoPreview,
	/// The walk ran out of BYTES before it reached a `preview` attribute. Distinct from NoPreview on
	/// purpose: the document may well carry a preview further in, and rendering blank here would make
	/// artwork disappear with nothing to say why. Only a buffer decode can report this — a whole-file
	/// walk that ends without a preview has genuinely seen everything.
	Truncated,
	/// The document would not open: not found, not a song, or a firmware version this build refuses.
	Error,
};

/// @brief Decode a song's preview artwork out of an in-memory document, doing no I/O whatsoever.
///
/// Runs the SAME tag walk the file-backed decode runs, over a borrowed buffer rather than a live file,
/// so it can be called where blocking on the storage tier is not allowed.
///
/// @param buffer     Document bytes, which may be a PREFIX of the file. Borrowed for the call only.
/// @param isJson     `true` to parse the buffer as JSON, `false` as XML. A buffer has no filename to
///                   infer this from, so the caller must say.
/// @param imageStore Destination pad rows, `kDisplayHeight` of them. Written ONLY where the preview
///                   supplies pads: rows the walk never reaches keep whatever the caller left there,
///                   which is why callers clear it first.
/// @return What the walk found; see SongPreviewOutcome.
/// @note Restores the global `activeDeserializer` to whatever it pointed at on entry, on every path
///       including a failed open. That restore is load-bearing, not hygiene: while the walk runs, that
///       global points at a reader whose `fileClusterBuffer` IS @p buffer, and code downstream WRITES
///       through the global without opening a document of its own (SaveSongUI's sample collection moves
///       a file into `activeDeserializer->file`, then reads `Cluster::size` -- 32 KiB -- at a time into
///       `activeDeserializer->fileClusterBuffer`). Reached with the global left here, that is a 32 KiB
///       write into a buffer that may be 4 KiB and may already have been freed: heap corruption, not a
///       stale read.
SongPreviewOutcome decodeSongPreview(std::span<const uint8_t> buffer, bool isJson,
                                     RGB (*imageStore)[kDisplayWidth + kSideBarWidth]);

/// @brief Decode a song's preview artwork by reading the song file, the synchronous way.
///
/// The storage-tier backing for the browser's preview, and the reference the buffer decode above is
/// held to. Opens @p path through the ordinary deserializer and runs the identical walk, so it blocks
/// on I/O and may only be called where that is allowed (the storage owner).
///
/// @param path       Absolute path of the song file on the card.
/// @param imageStore Destination pad rows, as for decodeSongPreview().
/// @param openError  Optionally receives the open failure behind a SongPreviewOutcome::Error return, so
///                   a UI caller can report which one it was. Untouched on any other outcome.
/// @return What the walk found; never SongPreviewOutcome::Truncated, since a file walk that ends
///         without a preview has read the whole document.
SongPreviewOutcome decodeSongPreviewFromFile(char const* path, RGB (*imageStore)[kDisplayWidth + kSideBarWidth],
                                             ::Error* openError = nullptr);

} // namespace deluge::storage
