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
/// @brief Weak, do-nothing definitions for `libdeluge/file_write.h`, plus the constants that pin the
///        boundary.
///
/// A BSP without the Rust write channel resolves these, `deluge_file_write_async_active()` returns
/// false, and `SampleRecorder` takes its synchronous arm -- which is the behaviour that shipped
/// before this channel existed. The Rust Embassy BSP overrides all three in `file_write.rs`.
///
/// Every function here is `noexcept`, like every function in `file_bytes_abi.cpp`: the repo's
/// allocators throw `deluge::exception::BAD_ALLOC` (operators.cpp), and letting anything unwind out
/// of an `extern "C"` frame into a Rust caller is undefined behaviour -- on this target it presents
/// as `std::terminate()` aborting silently, i.e. an unexplained device death with no error message.
/// None of these bodies can throw, so none carries a `catch (...)`; the marker is what keeps that
/// true if a body is ever added.

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

extern "C" {

__attribute__((weak)) bool deluge_file_write_request_write_at(uint32_t, uint32_t, const uint8_t*, uint32_t) noexcept {
	// Refused, so the caller takes its synchronous arm. Silently accepting and never answering is
	// the failure this project has hit three times (the startup fail-safe's boot wedge,
	// smsysex_pending, and the settings submit machine): a channel that accepts and never answers
	// strands its caller forever.
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_write_at(uint32_t, uint32_t*) noexcept {
	// UNKNOWN, not PENDING: "there is nothing in flight, ask again if you want one". PENDING would
	// tell a caller to keep waiting for an answer that can never come.
	return DELUGE_FILE_WRITE_UNKNOWN;
}

// The other six operations, same two rules throughout: a request is REFUSED so the caller takes its
// synchronous arm, and a collect answers UNKNOWN -- "nothing is in flight, ask again if you want
// one" -- never PENDING, which would tell a caller to keep waiting for an answer that can never come.

__attribute__((weak)) bool deluge_file_write_request_open(const char*, uint8_t) noexcept {
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_open(uint32_t*) noexcept {
	return DELUGE_FILE_WRITE_UNKNOWN;
}

__attribute__((weak)) bool deluge_file_write_request_read_at(uint32_t, uint32_t, uint8_t*, uint32_t) noexcept {
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_read_at(uint32_t, uint32_t*) noexcept {
	return DELUGE_FILE_WRITE_UNKNOWN;
}

__attribute__((weak)) bool deluge_file_write_request_truncate(uint32_t, uint32_t) noexcept {
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_truncate(uint32_t) noexcept {
	return DELUGE_FILE_WRITE_UNKNOWN;
}

__attribute__((weak)) bool deluge_file_write_request_close(uint32_t) noexcept {
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_close(uint32_t) noexcept {
	return DELUGE_FILE_WRITE_UNKNOWN;
}

__attribute__((weak)) bool deluge_file_write_request_stats(void) noexcept {
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_stats(uint32_t*) noexcept {
	return DELUGE_FILE_WRITE_UNKNOWN;
}

__attribute__((weak)) bool deluge_file_write_request_read_file(const char*, uint8_t*, uint32_t) noexcept {
	return false;
}

__attribute__((weak)) uint8_t deluge_file_write_collect_read_file(const char*, uint32_t*, uint32_t*) noexcept {
	return DELUGE_FILE_WRITE_UNKNOWN;
}

__attribute__((weak)) bool deluge_file_write_async_active(void) noexcept {
	return false;
}

} // extern "C"
