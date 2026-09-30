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
/// @brief Compile-time pins on `libdeluge/file_directory.h`: the constants the Rust side
/// (`src/bsp/rust/src/file_directory.rs`)
///        hand-duplicates, and the status values it shares with the other channels.

#include "libdeluge/file_directory.h"

#include "libdeluge/audio_load.h"  // the status values this header's must equal
#include "libdeluge/file_prefix.h" // ... and the sibling channel's, which share a caller

// The one STATUS the Rust module hand-duplicates (as `NO_FILESYSTEM`), because its driver branches on
// it: an open that reports "no filesystem" is what tells the driver to mount. Everything else it
// simply carries through, so this is the only value that has to agree.
static_assert(DELUGE_ERR_NO_FILESYSTEM == -11);

// The status bytes the Rust side hand-duplicates from this header (`const _` in file_directory.rs).
static_assert(DELUGE_FILE_DIRECTORY_PENDING == 0u);
static_assert(DELUGE_FILE_DIRECTORY_READY == 1u);
static_assert(DELUGE_FILE_DIRECTORY_UNKNOWN == 2u);
static_assert(DELUGE_FILE_DIRECTORY_FAILED == 3u);

// The cross-header pin, and the reason this TU includes three headers rather than one. A browser
// tick reaches all three channels -- it lists a folder, it draws that folder's song artwork, and it
// loads the samples underneath -- so all three sets of macros are routinely in scope together,
// where `deluge_file_directory_collect(..) == DELUGE_FILE_PREFIX_READY` compiles clean and nothing
// else cross-checks it. Under a divergent numbering that expression would silently mean something
// other than what it reads as. bindgen's allowlist covers `Deluge.*` TYPES, not macros, so this is
// the ONLY thing that checks it.
static_assert(DELUGE_FILE_DIRECTORY_PENDING == DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_DIRECTORY_READY == DELUGE_AUDIO_LOAD_READY);
static_assert(DELUGE_FILE_DIRECTORY_UNKNOWN == DELUGE_AUDIO_LOAD_UNKNOWN);
static_assert(DELUGE_FILE_DIRECTORY_PENDING == DELUGE_FILE_PREFIX_PENDING);
static_assert(DELUGE_FILE_DIRECTORY_READY == DELUGE_FILE_PREFIX_READY);
static_assert(DELUGE_FILE_DIRECTORY_UNKNOWN == DELUGE_FILE_PREFIX_UNKNOWN);
static_assert(DELUGE_FILE_DIRECTORY_FAILED == DELUGE_FILE_PREFIX_FAILED);

// FAILED has no counterpart in audio_load.h, and must not collide with one -- see
// file_prefix_abi.cpp's identical trio for why a fourth value equal to a loader status would make
// the cross-header confusion worse rather than better.
static_assert(DELUGE_FILE_DIRECTORY_FAILED != DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_DIRECTORY_FAILED != DELUGE_AUDIO_LOAD_READY);
static_assert(DELUGE_FILE_DIRECTORY_FAILED != DELUGE_AUDIO_LOAD_UNKNOWN);
