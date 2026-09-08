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
/// unmodified from the request until the matching collect returns @ref DELUGE_FILE_WRITE_READY or
/// @ref DELUGE_FILE_WRITE_FAILED. Nothing below the caller enforces this: `deluge::io::Stream::close()`
/// deliberately does NOT drain first (see the note at its definition -- every wait primitive it could
/// use is unavailable in the contexts it runs in), so the obligation belongs to the caller's own
/// state machine, which must reach a terminal collect before it frees or closes.
///
/// **Single-flight, keyed by handle.** `SampleRecorder` writes one cluster per tick, so it has one
/// write outstanding by construction. A second request for a handle that already has one is
/// REFUSED (returns false) rather than queued or replaced: a dropped cluster write is lost audio,
/// so the caller must see the refusal and retry.
///
/// **Seven operations, six keyed by handle and one by path.** The write came first; open, read,
/// truncate, close and stats followed once the recorder's finalize and create needed them off the
/// owner too. @ref deluge_file_write_request_read_file is the odd one out: it is keyed by PATH
/// because it does its own open/read/close, for a caller that wants a whole file and nothing else.
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
bool deluge_file_write_request_write_at(uint32_t handle, uint32_t offset, const uint8_t* bytes,
                                        uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the answer for `handle`'s in-flight write.
///
/// @param handle      The handle passed to @ref deluge_file_write_request_write_at.
/// @param out_written Written ONLY on @ref DELUGE_FILE_WRITE_READY, with the true byte count. A
///                    count below the requested `len` is a short write; the caller treats it as a
///                    card error.
/// @return one of the four statuses above.
uint8_t deluge_file_write_collect_write_at(uint32_t handle, uint32_t* out_written) DELUGE_NOEXCEPT;

/// @brief Ask for `path` to be opened for writing.
///
/// The only PATH-keyed operation here: there is no handle yet. The handle arrives in the collect.
/// @param path NUL-terminated path.
/// @param mode `DelugeStreamMode` as a fixed-width byte (0 read, 1 create, 2 create-always,
///             3 append: open existing, no truncation).
/// @return false when refused -- another operation is in flight, or the channel is not running.
bool deluge_file_write_request_open(const char* path, uint8_t mode) DELUGE_NOEXCEPT;

/// @brief Collect the open.
/// @param out_handle Written ONLY on @ref DELUGE_FILE_WRITE_READY. ZERO IS A VALID HANDLE -- see the
///                   note on @ref deluge_file_write_request_write_at.
uint8_t deluge_file_write_collect_open(uint32_t* out_handle) DELUGE_NOEXCEPT;

/// @brief Ask for `len` bytes at `offset` to be read into `dst`.
///
/// Goes through the HANDLE, not a path, and that is load-bearing: it reads bounded by the handle's
/// live, not-yet-flushed size (`deluge_efatfs_stream_read_at_via`). A path-keyed read would see the
/// on-disk extent instead, which during finalize is the wrong number.
/// @param handle A stream-write handle. Zero is valid.
/// @param offset Absolute byte offset within the file.
/// @param dst    Caller-owned destination. Must stay alive and unmodified until the collect is
///               terminal. NOT copied.
/// @param len    Byte count to read.
/// @return false when refused; the caller must retry or take its synchronous arm.
bool deluge_file_write_request_read_at(uint32_t handle, uint32_t offset, uint8_t* dst, uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the read.
/// @param handle   The handle passed to @ref deluge_file_write_request_read_at.
/// @param out_read Written ONLY on @ref DELUGE_FILE_WRITE_READY, with the TRUE byte count, which may
///                 be short of `len` at EOF. The caller decides whether short is an error; for the
///                 recorder mid-transform it is.
uint8_t deluge_file_write_collect_read_at(uint32_t handle, uint32_t* out_read) DELUGE_NOEXCEPT;

/// @brief Ask for the file behind `handle` to be truncated to `size` bytes.
/// @return false when refused.
bool deluge_file_write_request_truncate(uint32_t handle, uint32_t size) DELUGE_NOEXCEPT;

/// @brief Collect the truncate.
uint8_t deluge_file_write_collect_truncate(uint32_t handle) DELUGE_NOEXCEPT;

/// @brief Ask for `handle` to be closed, flushing its deferred directory entry.
///
/// A caller that will DELETE this file must wait for the collect to be terminal before unlinking.
/// Closing flushes the size/first_cluster/mtime edit deferred for the whole recording; unlinking
/// first leaves a stale near-empty entry with `first_cluster` still ~0, frees no clusters, and lets a
/// later file that recycles the freed short-name slot receive this handle's flush.
/// @return false when refused.
bool deluge_file_write_request_close(uint32_t handle) DELUGE_NOEXCEPT;

/// @brief Collect the close.
uint8_t deluge_file_write_collect_close(uint32_t handle) DELUGE_NOEXCEPT;

/// @brief Ask for the filesystem's free-cluster count.
///
/// Exists for the recorder's pre-flight space check, and doubles as the only instrument that can see
/// a leaked cluster chain -- an ordering bug that deletes a file before closing it is invisible to
/// any byte comparison, because the file is gone either way.
/// @return false when refused.
bool deluge_file_write_request_stats(void) DELUGE_NOEXCEPT;

/// @brief Collect the free-cluster count.
/// @param out_free_clusters Written ONLY on @ref DELUGE_FILE_WRITE_READY.
uint8_t deluge_file_write_collect_stats(uint32_t* out_free_clusters) DELUGE_NOEXCEPT;

/// @brief Read the whole of `path` into the caller's buffer.
///
/// PATH-keyed, unlike the handle operations above, because it does its own open/read/close
/// internally -- one request instead of four, for a caller that wants a whole file and nothing else.
///
/// @param path NUL-terminated path.
/// @param dst  Caller-owned destination. Must stay alive and unmodified until the collect is
///             terminal. NOT copied: the pointer crosses the boundary, the bytes do not.
/// @param cap  Capacity of `dst`. A file larger than this is REFUSED, never truncated -- a truncated
///             document handed to a parser is worse than a failure.
/// @return false when the request was refused; the caller must retry.
bool deluge_file_write_request_read_file(const char* path, uint8_t* dst, uint32_t cap) DELUGE_NOEXCEPT;

/// @brief Collect the whole-file read.
///
/// @param path       The path passed to @ref deluge_file_write_request_read_file.
/// @param out_len    Written ONLY on @ref DELUGE_FILE_WRITE_READY: the file's size, now in `dst`.
/// @param out_needed Written on @ref DELUGE_FILE_WRITE_FAILED when the file did not FIT: the size
///                   required. ZERO on a genuine I/O failure, which is how the caller tells a
///                   too-small buffer from a broken card. Sizing with a stat first does not make
///                   this redundant -- it is what catches the file changing in between.
uint8_t deluge_file_write_collect_read_file(const char* path, uint32_t* out_len, uint32_t* out_needed) DELUGE_NOEXCEPT;

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
