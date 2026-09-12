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
/// @brief Weak, do-nothing definitions for `libdeluge/file_bytes.h`, plus the constants that pin the
///        boundary.
///
/// A BSP without the Rust byte channel resolves these, and every caller then takes its own
/// synchronous arm -- `deluge_file_bytes_async_active()` returns false, which is the whole of what a
/// caller consults. The Rust Embassy BSP overrides all seven in `file_bytes.rs`.
///
/// Every function here is `noexcept`, like every function in `file_directory_abi.cpp`,
/// `file_prefix_abi.cpp`, `audio_load_abi.cpp` and `card_abi.cpp`: the repo's allocators throw
/// `deluge::exception::BAD_ALLOC` (operators.cpp), and letting anything unwind out of an
/// `extern "C"` frame into a Rust caller is undefined behaviour -- on this target it presents as
/// `std::terminate()` aborting silently, i.e. an unexplained device death with no error message.
/// None of these bodies can throw (they neither allocate nor call anything that does), so none
/// carries a `catch (...)`; the marker is what keeps that true if a body is ever added.

#include "libdeluge/file_bytes.h"

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
//
// Rung 2 shipped its two channels with the numbering misaligned and caught it in review rather than
// by compiler; this file is that lesson applied before the fourth channel has a caller.
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

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_unlink(const char* /*path*/) noexcept {
	// REFUSED. Reporting the refusal honestly matters: a caller told `true` for a deletion that will
	// never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_unlink(const char* /*path*/,
                                                                          DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) void deluge_file_bytes_request_exists(const char* /*path*/) noexcept {
	// No byte channel on this BSP/config; the caller never reaches here because
	// deluge_file_bytes_async_active() is false.
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_exists(const char* /*path*/, bool* /*out_present*/,
                                                                          DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not FAILED. FAILED is terminal and would be read as `StartupPresence::Unusable`, which
	// would make a build without the channel skip the startup song forever rather than take its
	// synchronous arm. This distinction is load-bearing -- do not "simplify" it.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_mkdir_all(const char* /*path*/) noexcept {
	// REFUSED. Reporting the refusal honestly matters: a caller told `true` for a creation that will
	// never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_mkdir_all(const char* /*path*/,
                                                                             DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_set_time(const char* /*path*/,
                                                                         DelugeTimestamp /*timestamp*/) noexcept {
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_set_time(const char* /*path*/,
                                                                            DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_read_at(const char* /*path*/, uint32_t /*offset*/,
                                                                        uint32_t /*len*/) noexcept {
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_read_at(const char* /*path*/, uint8_t* /*out_buf*/,
                                                                           uint32_t /*buf_cap*/, uint32_t* /*out_len*/,
                                                                           DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_write_at(const char* /*path*/, uint32_t /*offset*/,
                                                                         const uint8_t* /*bytes*/,
                                                                         uint32_t /*len*/) noexcept {
	// REFUSED. Reporting the refusal honestly matters: a caller told `true` for a write that will
	// never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_write_at(const char* /*path*/,
                                                                            uint32_t* /*out_written*/,
                                                                            DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) void deluge_file_bytes_request_stat(const char* /*path*/) noexcept {
	// No byte channel on this BSP/config; the caller never reaches here because
	// deluge_file_bytes_async_active() is false.
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_stat(const char* /*path*/, uint32_t* /*out_size*/,
                                                                        DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not FAILED, for the reason `collect_exists`'s fallback gives: FAILED is terminal, and a
	// caller reading it as a real answer would act on a size it never obtained.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_copy(const char* /*from*/,
                                                                     const char* /*to*/) noexcept {
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_copy(const char* /*to*/,
                                                                        DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_async_active() noexcept {
	return false;
}

#ifdef DELUGE_HOST
extern "C" __attribute__((weak)) void deluge_file_bytes_set_latency_ms(uint32_t /*ms*/) noexcept {
	// A host build without the Rust byte channel has no operations to delay.
}
#endif

extern "C" __attribute__((weak)) void deluge_file_bytes_request_read(const char* /*path*/) noexcept {
	// No byte channel on this BSP/config; the caller never reaches here because
	// deluge_file_bytes_async_active() is false. Dropping the request (rather than pretending to have
	// accepted it) is what the collect below already reports.
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_read(const char* /*path*/, uint8_t* /*out_buf*/,
                                                                        uint32_t /*buf_cap*/, uint32_t* /*out_len*/,
                                                                        DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not PENDING: PENDING would tell a caller to keep asking forever -- and a caller that
	// kept asking would never read the file, because on this build nothing is going to. No output is
	// touched, which is exactly what the header promises for any status but READY/FAILED.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_write(const char* /*path*/, const uint8_t* /*bytes*/,
                                                                      uint32_t /*len*/) noexcept {
	// REFUSED, and reporting the refusal honestly matters more here than in the read above: a caller
	// told `true` for a write that will never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_write(const char* /*path*/,
                                                                         DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" __attribute__((weak)) bool deluge_file_bytes_request_rename(const char* /*from*/,
                                                                       const char* /*to*/) noexcept {
	return false;
}

extern "C" __attribute__((weak)) uint8_t deluge_file_bytes_collect_rename(const char* /*to*/,
                                                                          DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}
