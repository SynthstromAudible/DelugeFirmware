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
/// @brief Compile-time pins on `libdeluge/file_write.h`: the constants the Rust side (`src/bsp/rust/src/file_write.rs`)
///        hand-duplicates, and the status values it shares with the other channels.

#include "libdeluge/file_write.h"

#include "libdeluge/audio_load.h" // the status values this header's must equal
#include "libdeluge/file_bytes.h"

// The four statuses, which every channel on this boundary shares. bindgen's allowlist covers types,
// not macros, so the Rust side hand-duplicates these numbers and nothing but this pins them
// together. A caller that switched on one channel's numbering while reading another's answers would
// read PENDING as READY -- for this channel, "the cluster is on the card" when it is not.
static_assert(DELUGE_FILE_WRITE_PENDING == DELUGE_FILE_BYTES_PENDING);
static_assert(DELUGE_FILE_WRITE_READY == DELUGE_FILE_BYTES_READY);
static_assert(DELUGE_FILE_WRITE_UNKNOWN == DELUGE_FILE_BYTES_UNKNOWN);
static_assert(DELUGE_FILE_WRITE_FAILED == DELUGE_FILE_BYTES_FAILED);
static_assert(DELUGE_FILE_WRITE_PENDING == DELUGE_AUDIO_LOAD_PENDING);
static_assert(DELUGE_FILE_WRITE_READY == DELUGE_AUDIO_LOAD_READY);
