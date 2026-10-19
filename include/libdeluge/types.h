/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// libdeluge/types.h — shared POD types for the hardware boundary.
///
/// Everything here is C-ABI: fixed-width integers, bool, and plain structs only.
#ifndef LIBDELUGE_TYPES_H
#define LIBDELUGE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Result/status code returned by fallible boundary calls.
typedef enum DelugeStatus : int8_t {
	DELUGE_OK = 0,
	DELUGE_ERR = -1,         ///< unspecified failure
	DELUGE_ERR_PARAM = -2,   ///< invalid argument
	DELUGE_ERR_BUSY = -3,    ///< resource temporarily unavailable
	DELUGE_ERR_TIMEOUT = -4, ///< operation timed out
	DELUGE_ERR_IO = -5,      ///< hardware / transport I/O error
	DELUGE_ERR_NODEV = -6,   ///< no such device / not present
	DELUGE_ERR_UNSUPPORTED = -7,
	DELUGE_ERR_NOT_FOUND = -8,        ///< path does not exist
	DELUGE_ERR_EXISTS = -9,           ///< path already exists
	DELUGE_ERR_NO_SPACE = -10,        ///< storage full / allocation failed
	DELUGE_ERR_NO_FILESYSTEM = -11,   ///< media present but has no valid filesystem
	DELUGE_ERR_WRITE_PROTECTED = -12, ///< media is read-only / locked
	DELUGE_ERR_NO_MEMORY = -13,       ///< RAM allocation failed (distinct from NO_SPACE, which is storage/disk space)
	DELUGE_ERR_NOT_EMPTY = -14,       ///< directory not empty
} DelugeStatus;

/// A handle to a row of one of the BSP's tables: an open file, directory or stream.
///
/// 0 is none (@ref DELUGE_HANDLE_NONE), and no call ever hands it out. A caller stores the number as it came and
/// tests it against @ref DELUGE_HANDLE_NONE. A call that takes a handle and is passed @ref DELUGE_HANDLE_NONE does
/// nothing and answers its bad-argument result: a status-returning call answers @ref DELUGE_ERR_PARAM, a close that
/// returns nothing does nothing, a channel request answers false and a channel collect answers
/// `DELUGE_CHANNEL_UNKNOWN`. A non-zero handle that names no open row is refused by each call's own rule, which is
/// not always the same answer (the streaming `deluge_efatfs_read_at` and `deluge_efatfs_stream_*` calls answer
/// @ref DELUGE_ERR_IO for it).
typedef uint32_t DelugeHandle;

/// The handle that names nothing. See @ref DelugeHandle.
#define DELUGE_HANDLE_NONE 0u

/// Bytes in @ref DelugeFileLocation::context.
#define DELUGE_FILE_LOCATION_BYTES 64

/// Where a file is on the card, as plain data: enough to open it again without looking it up by path, which on a
/// large folder means walking every entry before it.
///
/// Owns nothing, so a copy can be dropped at any time. Valid only on the mount it records
/// (deluge_efatfs_mount_generation): an open from it after the card has been remounted, or swapped, is refused.
typedef struct DelugeFileLocation {
	uint8_t context[DELUGE_FILE_LOCATION_BYTES]; ///< the filesystem's own record of the file, opaque here
	uint32_t mount_generation;                   ///< the mount the file was found on
	uint8_t valid;                               ///< nonzero if this names a file
} DelugeFileLocation;

/// The resource manager chunks are leased from. Opaque; owned by the Rust `deluge_resource` crate.
typedef struct DelugeResource DelugeResource;

/// A sample's stream: the C++ `deluge::audio::stream::SampleStream` a region-port cursor is opened
/// over. Opaque; only the C++ side dereferences it.
typedef struct DelugeStreamBacking DelugeStreamBacking;

/// A resident streamed chunk the fill queue fills: the Rust `deluge_sample_fill` crate's
/// `StreamedChunk`. Opaque; only the Rust side dereferences it.
typedef struct DelugeChunkBacking DelugeChunkBacking;

/// A portable, timezone-free timestamp — matches FAT DOS date/time's own
/// decomposition (year/month/day/hour/minute/second, no epoch, no TZ) so no
/// backend needs to invent a timezone. A DOS-backed implementation rounds
/// `second` down to its native 2-second resolution.
typedef struct DelugeTimestamp {
	uint16_t year;  ///< full year, e.g. 2026 (not DOS's 1980-relative offset)
	uint8_t month;  ///< 1-12
	uint8_t day;    ///< 1-31
	uint8_t hour;   ///< 0-23
	uint8_t minute; ///< 0-59
	uint8_t second; ///< 0-59
} DelugeTimestamp;

/// One stereo frame of audio. Layout-compatible with the application's
/// `StereoSample` (`{ q31_t l; q31_t r; }`). Samples are signed Q31 fixed point.
typedef struct DelugeStereoSample {
	int32_t l;
	int32_t r;
} DelugeStereoSample;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_TYPES_H
