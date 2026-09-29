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
/// @brief Compile-time pins on `libdeluge/file_bytes.h`: the constants the Rust side (`src/bsp/rust/src/file_bytes.rs`)
///        hand-duplicates, and the status values it shares with the other channels.

#include "libdeluge/file_bytes.h"

#include <cstddef>

#include "libdeluge/audio_load.h"     // the status values this header's must equal
#include "libdeluge/file_directory.h" // ... and the two sibling channels'
#include "libdeluge/file_prefix.h"

// The path bound, which this header spells for its callers and the Rust side hand-duplicates as
// `MAX_PATH_CHARS` (255 = this minus the NUL). Equal to the other channels' by construction: they
// key the same filesystem, and a path one channel can name but another cannot would be a trap.
static_assert(DELUGE_FILE_BYTES_MAX_PATH == DELUGE_MAX_PATH);
static_assert(DELUGE_FILE_BYTES_MAX_PATH == DELUGE_FILE_DIRECTORY_MAX_PATH);

// The byte bound, hand-duplicated on the Rust side as `MAX_BYTES`. Not derived from anything: it is
// set by measurement against real settings files and against what the device image will link with
// two staging buffers of this size. See the header.
static_assert(DELUGE_FILE_BYTES_MAX_BYTES == 8192u);

// The block bound, hand-duplicated on the Rust side as `BLOCK_BYTES`. Pinned here because the C++
// consumer's own `blockBufferMax` must agree with it, and nothing else checks that they do.
static_assert(DELUGE_FILE_BYTES_BLOCK == 1024u);

// The Rust byte channel's FileTimestamp mirrors this layout and asserts the same numbers.
static_assert(sizeof(DelugeTimestamp) == 8);
static_assert(alignof(DelugeTimestamp) == 2);
static_assert(offsetof(DelugeTimestamp, month) == 2);
static_assert(offsetof(DelugeTimestamp, second) == 6);

// The two STATUSES the Rust module hand-duplicates because its driver produces them itself rather
// than carrying them through: NO_FILESYSTEM is what tells the driver to mount, NO_MEMORY is what it
// reports for a file too large for the staging buffer, and IO is what it reports for a short read or
// a short write. Everything else it simply carries through.
static_assert(DELUGE_ERR_NO_FILESYSTEM == -11);
static_assert(DELUGE_ERR_NO_MEMORY == -13);
static_assert(DELUGE_ERR_IO == -5);

// The status bytes the Rust side hand-duplicates from this header (`const _` in file_bytes.rs).
static_assert(DELUGE_FILE_BYTES_PENDING == 0u);
static_assert(DELUGE_FILE_BYTES_READY == 1u);
static_assert(DELUGE_FILE_BYTES_UNKNOWN == 2u);
static_assert(DELUGE_FILE_BYTES_FAILED == 3u);

// The cross-header pin, and the reason this TU includes four headers rather than one. These macro
// sets are routinely in scope together, where `deluge_file_bytes_collect_read(..) ==
// DELUGE_FILE_DIRECTORY_READY` compiles clean and nothing else cross-checks it. Under a divergent
// numbering that expression would silently mean something other than what it reads as. bindgen's
// allowlist covers `Deluge.*` TYPES, not macros, so this is the ONLY thing that checks it.
static_assert(DELUGE_FILE_BYTES_PENDING == DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_BYTES_READY == DELUGE_AUDIO_LOAD_READY);
static_assert(DELUGE_FILE_BYTES_UNKNOWN == DELUGE_AUDIO_LOAD_UNKNOWN);
static_assert(DELUGE_FILE_BYTES_PENDING == DELUGE_FILE_PREFIX_PENDING);
static_assert(DELUGE_FILE_BYTES_READY == DELUGE_FILE_PREFIX_READY);
static_assert(DELUGE_FILE_BYTES_UNKNOWN == DELUGE_FILE_PREFIX_UNKNOWN);
static_assert(DELUGE_FILE_BYTES_FAILED == DELUGE_FILE_PREFIX_FAILED);
static_assert(DELUGE_FILE_BYTES_PENDING == DELUGE_FILE_DIRECTORY_PENDING);
static_assert(DELUGE_FILE_BYTES_READY == DELUGE_FILE_DIRECTORY_READY);
static_assert(DELUGE_FILE_BYTES_UNKNOWN == DELUGE_FILE_DIRECTORY_UNKNOWN);
static_assert(DELUGE_FILE_BYTES_FAILED == DELUGE_FILE_DIRECTORY_FAILED);

// FAILED has no counterpart in audio_load.h, and must not collide with one -- see
// file_prefix_abi.cpp's identical trio for why a fourth value equal to a loader status would make
// the cross-header confusion worse rather than better.
static_assert(DELUGE_FILE_BYTES_FAILED != DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_BYTES_FAILED != DELUGE_AUDIO_LOAD_READY);
static_assert(DELUGE_FILE_BYTES_FAILED != DELUGE_AUDIO_LOAD_UNKNOWN);
