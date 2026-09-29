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
/// @brief Compile-time pins on `libdeluge/file_prefix.h`: the constants the Rust side
///        (`src/bsp/rust/src/file_prefix.rs`) hand-duplicates, and the status values it shares with the audio loader.

#include "libdeluge/file_prefix.h"

#include "libdeluge/audio_load.h" // the status values this header's must equal
#include "storage/song_preview.h" // kSongPreviewWindow

// The one constant the two sides of this boundary hand-duplicate. `deluge_file_prefix_request`
// clamps to it and the Rust slot is sized by it, so a drift would not fail to link or to build --
// it would silently hand a decoder fewer bytes than the window it was measured for, and a song whose
// `preview` attribute sat past the shortened window would render BLANK, which is indistinguishable
// from a song that has no preview at all. src/bsp/rust/src/file_prefix.rs's `PREFIX_CAPACITY` is the
// Rust half of this pin.
//
// This is a BOUND, not an equality. The two constants answer different questions -- kSongPreviewWindow is
// "how much of a song holds the artwork", DELUGE_FILE_PREFIX_CAPACITY is "how big is the slot every
// request's result lands in" -- and only one relation between them is load-bearing: a request larger
// than the slot could never be served whole.
static_assert(deluge::storage::kSongPreviewWindow <= DELUGE_FILE_PREFIX_CAPACITY,
              "a request larger than the channel's slot can never be served whole");

// The status bytes the Rust side hand-duplicates from this header (`const _` in file_prefix.rs).
static_assert(DELUGE_FILE_PREFIX_PENDING == 0u);
static_assert(DELUGE_FILE_PREFIX_READY == 1u);
static_assert(DELUGE_FILE_PREFIX_UNKNOWN == 2u);

static_assert(DELUGE_FILE_PREFIX_FAILED == 3u);

// And the same three shared values against the audio loader's, which a caller may well have in scope
// at the same time: `deluge_file_prefix_collect(..) == DELUGE_AUDIO_LOAD_READY` compiles clean either
// way, so equality is what keeps it from silently meaning PENDING -- see file_prefix.h's note above
// the macros. This is the ONLY thing that cross-checks the two sets; bindgen's allowlist does not.
static_assert(DELUGE_FILE_PREFIX_PENDING == DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_PREFIX_READY == DELUGE_AUDIO_LOAD_READY);
static_assert(DELUGE_FILE_PREFIX_UNKNOWN == DELUGE_AUDIO_LOAD_UNKNOWN);

// FAILED has NO counterpart in audio_load.h, and must not collide with one. The loader has three
// statuses; a fourth value that happened to equal one of them would make the cross-header confusion
// above worse rather than better, because it would read as a MEANINGFUL loader status.
static_assert(DELUGE_FILE_PREFIX_FAILED != DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_PREFIX_FAILED != DELUGE_AUDIO_LOAD_READY);
static_assert(DELUGE_FILE_PREFIX_FAILED != DELUGE_AUDIO_LOAD_UNKNOWN);
