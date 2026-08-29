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

/// libdeluge/file_prefix.h's C++-side fallback, plus the two constants that pin the boundary.

/// @file
/// @brief Weak, synchronous-backing definitions for `libdeluge/file_directory.h`.
///
/// A BSP without the Rust directory channel resolves these, and every caller then takes its own
/// synchronous arm -- `deluge_file_directory_async_active()` returns false, which is the whole of
/// what a caller consults. The Rust Embassy BSP overrides all three in `file_directory.rs`.
///
/// Every function here is `noexcept`, like every function in `file_prefix_abi.cpp`,
/// `audio_load_abi.cpp` and `card_abi.cpp`: the repo's allocators throw
/// `deluge::exception::BAD_ALLOC` (operators.cpp), and letting anything unwind out of an
/// `extern "C"` frame into a Rust caller is undefined behaviour -- on this target it presents as
/// `std::terminate()` aborting silently, i.e. an unexplained device death with no error message.
/// None of these bodies can throw (they neither allocate nor call anything that does), so none
/// carries a `catch (...)`; the marker is what keeps that true if a body is ever added.

#include "libdeluge/file_directory.h"

#include "libdeluge/audio_load.h"  // the status values this header's must equal
#include "libdeluge/file_prefix.h" // ... and the sibling channel's, which share a caller

// The path bound, which this header spells for its callers and the Rust side hand-duplicates as
// `MAX_PATH_CHARS` (255 = this minus the NUL). Equal to the loader's by construction: they key the
// same filesystem, and a path one channel can name but the other cannot would be a trap for the
// browser tick that reaches both.
static_assert(DELUGE_FILE_DIRECTORY_MAX_PATH == DELUGE_MAX_PATH);

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
//
// Rung 2 shipped its two channels with the numbering misaligned and caught it in review rather than
// by compiler; this file is that lesson applied before the third channel has a caller.
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

extern "C" __attribute__((weak)) bool deluge_file_directory_async_active() noexcept {
	return false;
}

extern "C" __attribute__((weak)) void deluge_file_directory_request(const char* /*dir_path*/,
                                                                    bool /*create_if_missing*/) noexcept {
	// No directory channel on this BSP/config; the caller never reaches here because
	// deluge_file_directory_async_active() is false. Dropping the request (rather than pretending to
	// have accepted it) is what the collect below already reports.
}

extern "C" __attribute__((weak)) uint8_t deluge_file_directory_collect(const char* /*dir_path*/,
                                                                       uint32_t* /*out_handle*/,
                                                                       DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not PENDING: PENDING would tell a caller to keep asking forever -- and a caller that
	// kept asking would never open the folder, because on this build nothing is going to. Neither
	// output is touched, which is exactly what the header promises for any status but READY/FAILED.
	return DELUGE_FILE_DIRECTORY_UNKNOWN;
}
