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
/// @brief The storage channels' C ABI for the unit tests, which link no BSP: `libdeluge/file_stream.h`,
///        `file_bytes.h` and `file_directory.h`, answered as "no channel here".
///
/// The Rust BSP defines every one of these in `file_stream.rs`, `file_bytes.rs` and `file_directory.rs`. The code under
/// test reaches them only past a boot the unit tests never finish, apart from the two `holds` calls, which answer
/// that no read holds a buffer. A request is refused so the caller takes its synchronous arm; a collect answers
/// UNKNOWN, never PENDING, which would keep a caller waiting for an answer that can never come.

#include "libdeluge/file_bytes.h"
#include "libdeluge/file_directory.h"
#include "libdeluge/file_stream.h"

extern "C" {

bool deluge_file_stream_request_write_at(uint32_t, uint32_t, const uint8_t*, uint32_t) noexcept {
	// Refused, so the caller takes its synchronous arm. Silently accepting and never answering is
	// the failure this project has hit three times (the startup fail-safe's boot wedge,
	// smsysex_pending, and the settings submit machine): a channel that accepts and never answers
	// strands its caller forever.
	return false;
}

uint8_t deluge_file_stream_collect_write_at(uint32_t, uint32_t*) noexcept {
	// UNKNOWN, not PENDING: "there is nothing in flight, ask again if you want one". PENDING would
	// tell a caller to keep waiting for an answer that can never come.
	return DELUGE_FILE_STREAM_UNKNOWN;
}

// The other operations, same two rules throughout: a request is REFUSED so the caller takes its
// synchronous arm, and a collect answers UNKNOWN -- "nothing is in flight, ask again if you want
// one" -- never PENDING, which would tell a caller to keep waiting for an answer that can never come.

bool deluge_file_stream_request_open(const char*, uint8_t) noexcept {
	return false;
}

DelugeChannelStatus deluge_file_stream_collect_open(const char*, uint32_t*, DelugeStatus*) noexcept {
	return DELUGE_FILE_STREAM_UNKNOWN;
}

bool deluge_file_stream_request_read_at(uint32_t, uint32_t, uint8_t*, uint32_t) noexcept {
	return false;
}

uint8_t deluge_file_stream_collect_read_at(uint32_t, uint32_t*) noexcept {
	return DELUGE_FILE_STREAM_UNKNOWN;
}

bool deluge_file_stream_request_truncate(uint32_t, uint32_t) noexcept {
	return false;
}

uint8_t deluge_file_stream_collect_truncate(uint32_t) noexcept {
	return DELUGE_FILE_STREAM_UNKNOWN;
}

bool deluge_file_stream_request_close(uint32_t) noexcept {
	return false;
}

uint8_t deluge_file_stream_collect_close(uint32_t) noexcept {
	return DELUGE_FILE_STREAM_UNKNOWN;
}

bool deluge_file_stream_request_stats(void) noexcept {
	return false;
}

uint8_t deluge_file_stream_collect_stats(uint32_t*) noexcept {
	return DELUGE_FILE_STREAM_UNKNOWN;
}

bool deluge_file_stream_holds(const void*) noexcept {
	return false;
}

bool deluge_file_bytes_request_read_into(const char*, void*, uint32_t) noexcept {
	return false;
}

DelugeChannelStatus deluge_file_bytes_collect_read_into(const char*, uint32_t*, DelugeStatus*, uint32_t*) noexcept {
	return DELUGE_CHANNEL_UNKNOWN;
}

bool deluge_file_bytes_holds(const void*) noexcept {
	return false;
}

bool deluge_file_bytes_request_write_from(const char*, const void*, uint32_t, bool) noexcept {
	return false;
}

DelugeChannelStatus deluge_file_bytes_collect_write_from(const char*, uint32_t*, DelugeStatus*) noexcept {
	return DELUGE_CHANNEL_UNKNOWN;
}

uint32_t deluge_file_bytes_progress() noexcept {
	// No channel, so no work ever finishes: every waiting tick counts.
	return 0;
}

} // extern "C"

extern "C" bool deluge_file_bytes_request_unlink(const char* /*path*/) noexcept {
	// REFUSED. Reporting the refusal honestly matters: a caller told `true` for a deletion that will
	// never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_unlink(const char* /*path*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" void deluge_file_bytes_request_exists(const char* /*path*/) noexcept {
	// No byte channel on the unit tests; the caller never reaches here because boot never finishes.
}

extern "C" uint8_t deluge_file_bytes_collect_exists(const char* /*path*/, bool* /*out_present*/,
                                                    DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not FAILED. FAILED is terminal and would be read as `StartupPresence::Unusable`, which
	// would make a caller that has not finished booting skip the startup song forever rather than take its
	// synchronous arm. This distinction is load-bearing -- do not "simplify" it.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" void deluge_file_bytes_request_dir_exists(const char* /*path*/) noexcept {
	// No byte channel on the unit tests; see deluge_file_bytes_request_exists.
}

extern "C" uint8_t deluge_file_bytes_collect_dir_exists(const char* /*path*/, bool* /*out_present*/,
                                                        DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not FAILED, for the reason deluge_file_bytes_collect_exists gives.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_mkdir_all(const char* /*path*/) noexcept {
	// REFUSED. Reporting the refusal honestly matters: a caller told `true` for a creation that will
	// never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_mkdir_all(const char* /*path*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_set_time(const char* /*path*/, DelugeTimestamp /*timestamp*/) noexcept {
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_set_time(const char* /*path*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_read_at(const char* /*path*/, uint32_t /*offset*/,
                                                  uint32_t /*len*/) noexcept {
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_read_at(const char* /*path*/, uint8_t* /*out_buf*/, uint32_t /*buf_cap*/,
                                                     uint32_t* /*out_len*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_write_at(const char* /*path*/, uint32_t /*offset*/, const uint8_t* /*bytes*/,
                                                   uint32_t /*len*/) noexcept {
	// REFUSED. Reporting the refusal honestly matters: a caller told `true` for a write that will
	// never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_write_at(const char* /*path*/, uint32_t* /*out_written*/,
                                                      DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" void deluge_file_bytes_request_stat(const char* /*path*/) noexcept {
	// No byte channel on the unit tests; the caller never reaches here because boot never finishes.
}

extern "C" uint8_t deluge_file_bytes_collect_stat(const char* /*path*/, uint32_t* /*out_size*/,
                                                  DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not FAILED, for the reason `collect_exists`'s fallback gives: FAILED is terminal, and a
	// caller reading it as a real answer would act on a size it never obtained.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_copy(const char* /*from*/, const char* /*to*/) noexcept {
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_copy(const char* /*to*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" void deluge_file_bytes_request_read(const char* /*path*/) noexcept {
	// No byte channel on the unit tests; the caller never reaches here because boot never finishes.
	// Dropping the request (rather than pretending to have
	// accepted it) is what the collect below already reports.
}

extern "C" uint8_t deluge_file_bytes_collect_read(const char* /*path*/, uint8_t* /*out_buf*/, uint32_t /*buf_cap*/,
                                                  uint32_t* /*out_len*/, DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not PENDING: PENDING would tell a caller to keep asking forever -- and a caller that
	// kept asking would never read the file, because on this build nothing is going to. No output is
	// touched, which is exactly what the header promises for any status but READY/FAILED.
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_write(const char* /*path*/, const uint8_t* /*bytes*/,
                                                uint32_t /*len*/) noexcept {
	// REFUSED, and reporting the refusal honestly matters more here than in the read above: a caller
	// told `true` for a write that will never happen waits for a collect that never turns terminal.
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_write(const char* /*path*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" bool deluge_file_bytes_request_rename(const char* /*from*/, const char* /*to*/) noexcept {
	return false;
}

extern "C" uint8_t deluge_file_bytes_collect_rename(const char* /*to*/, DelugeStatus* /*out_status*/) noexcept {
	return DELUGE_FILE_BYTES_UNKNOWN;
}

extern "C" void deluge_file_directory_request(const char* /*dir_path*/, bool /*create_if_missing*/) noexcept {
	// No directory channel on the unit tests. No caller reaches here: every caller selects the channel only once
	// boot has finished, which a build linking this fallback never reports. Dropping the request
	// (rather than pretending to have accepted it) is what the collect below already reports.
}

extern "C" uint8_t deluge_file_directory_collect(const char* /*dir_path*/, uint32_t* /*out_handle*/,
                                                 DelugeStatus* /*out_status*/) noexcept {
	// UNKNOWN, not PENDING: PENDING would tell a caller to keep asking forever -- and a caller that
	// kept asking would never open the folder, because on this build nothing is going to. Neither
	// output is touched, which is exactly what the header promises for any status but READY/FAILED.
	return DELUGE_FILE_DIRECTORY_UNKNOWN;
}
