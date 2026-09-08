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
/// @brief One offset-addressed write to an ALREADY-OPEN stream handle, without holding the caller.
///
/// Request/collect, the same non-blocking shape @ref file_bytes.h, @ref audio_load.h and
/// `streaming_fill.h` use. It exists for `SampleRecorder`, whose per-cluster write runs for the
/// whole length of a recording and is the recorder's dominant blocking.
///
/// **It carries no handle table.** One already exists: `efatfs_fs.rs`'s `STREAM_WRITE_CTX`, a
/// `HandleTable<MAX_STREAM_WRITE_HANDLES>` sized at 8 and documented as the recorder's own, with a
/// generation counter per slot so a deferred write-back can detect its slot having been recycled.
/// `stream_write_at` is already an async fn; `deluge_efatfs_stream_write_at` merely blocks on it.
/// This header adds the request/collect pair over that future and nothing else.
///
/// **The pointer crosses, not the bytes.** Zero-copy, the shape `streaming_fill.h`'s `uint8_t* dest`
/// already uses in the read direction. There is no staging buffer on either side, so this approaches
/// none of the device-link pressure @ref file_bytes.h's `DELUGE_FILE_BYTES_MAX_BYTES` documents.
///
/// **The caller owns the buffer until the collect is terminal.** `bytes` must stay alive and
/// unmodified from @ref deluge_file_write_request until @ref deluge_file_write_collect returns
/// @ref DELUGE_FILE_WRITE_READY or @ref DELUGE_FILE_WRITE_FAILED. `deluge::io::Stream::close()`
/// enforces this by draining any in-flight request before it closes the handle; nothing else may
/// free a buffer with a request outstanding.
///
/// **Single-flight, keyed by handle.** `SampleRecorder` writes one cluster per tick, so it has one
/// write outstanding by construction. A second request for a handle that already has one is
/// REFUSED (returns false) rather than queued or replaced: a dropped cluster write is lost audio,
/// so the caller must see the refusal and retry.
///
/// **Open, read, truncate and close are deliberately absent.** They stay synchronous on
/// `deluge_efatfs_stream_*`, because every one of their callers is on the storage owner, where
/// blocking is legal. Only the per-tick write needed to move.
///
/// **Fixed-width, never a native enum.** `arm-none-eabi` defaults to short enums, so a native enum
/// layout must never be relied on across this boundary.
#ifndef LIBDELUGE_FILE_WRITE_H
#define LIBDELUGE_FILE_WRITE_H

#include <stdbool.h>
#include <stdint.h>

/* Not currently parsed as C -- same situation as file_bytes.h, and the same forward-looking
 * hardening: `noexcept` is C++-only and invalid C syntax, so it is spelled through this macro
 * (#undef'd at the end of the file so it cannot leak into whatever includes this header next). */
#ifdef __cplusplus
#define DELUGE_NOEXCEPT noexcept
extern "C" {
#else
#define DELUGE_NOEXCEPT
#endif

/// @brief Requested, or running right now. Ask again on a later tick; do NOT re-request.
#define DELUGE_FILE_WRITE_PENDING 0u
/// @brief The write completed and its answer has been consumed. TERMINAL.
#define DELUGE_FILE_WRITE_READY 1u
/// @brief Nothing is known about this write. Request it (again). Also what a caller sees before its
///        first request, and what every caller sees on a BSP without the Rust channel.
#define DELUGE_FILE_WRITE_UNKNOWN 2u
/// @brief The write did not happen. TERMINAL -- do NOT re-request.
#define DELUGE_FILE_WRITE_FAILED 3u

/// @brief Ask for `len` bytes at `bytes` to be written to `handle` at absolute `offset`.
///
/// @param handle A stream-write handle from `deluge_efatfs_stream_open`. ZERO IS VALID -- this
///               table's slots are indexed from zero, which is why `deluge::io::Stream` boxes
///               `handle + 1` into its opaque pointer. Do not borrow `sample_stream.h`'s
///               `0 = invalid` convention; it belongs to a different registry.
/// @param offset Absolute byte offset within the file.
/// @param bytes  Caller-owned buffer. Must stay alive and unmodified until the matching collect is
///               terminal. NOT copied.
/// @param len    Byte count.
/// @return false when the request was refused -- `handle` already has a write in flight, the
///         channel is not running, or a pointer/length is unusable. The caller MUST retry or take
///         its synchronous arm; nothing has been queued.
bool deluge_file_write_request(uint32_t handle, uint32_t offset, const uint8_t* bytes, uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the answer for `handle`'s in-flight write.
///
/// @param handle      The handle passed to @ref deluge_file_write_request.
/// @param out_written Written ONLY on @ref DELUGE_FILE_WRITE_READY, with the true byte count. A
///                    count below the requested `len` is a short write; the caller treats it as a
///                    card error.
/// @return one of the four statuses above.
uint8_t deluge_file_write_collect(uint32_t handle, uint32_t* out_written) DELUGE_NOEXCEPT;

/// @brief Whether the Rust driver is running and this channel may be used at all.
///
/// The whole of what a caller consults before choosing an arm. False on every BSP that resolves the
/// weak fallbacks in `file_write_abi.cpp`.
bool deluge_file_write_async_active(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif
#undef DELUGE_NOEXCEPT

#endif // LIBDELUGE_FILE_WRITE_H
