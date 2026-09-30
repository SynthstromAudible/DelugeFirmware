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
/// @brief The stream channel: a file opened by path, then read, written, truncated and closed through its stream
///        handle, one operation at a time, without holding the caller.
///
/// Request/collect, the same non-blocking shape @ref file_bytes.h, @ref audio_load.h and
/// `streaming_fill.h` use. It exists for `SampleRecorder`, whose per-cluster write runs for the
/// whole length of a recording and is the recorder's dominant blocking.
///
/// **It carries no handle table.** One already exists: `storage/efatfs/device.rs`'s `STREAM_WRITE_CTX`, a
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
/// unmodified from the request until the matching collect returns @ref DELUGE_CHANNEL_READY or
/// @ref DELUGE_CHANNEL_FAILED. Nothing below the caller enforces this: `deluge::io::Stream::close()`
/// deliberately does NOT drain first (see the note at its definition -- every wait primitive it could
/// use is unavailable in the contexts it runs in), so the obligation belongs to the caller's own
/// state machine, which must reach a terminal collect before it frees or closes.
///
/// **Single-flight, keyed by handle.** `SampleRecorder` writes one cluster per tick, so it has one
/// write outstanding by construction. A second request for a handle that already has one is
/// REFUSED (returns false) rather than queued or replaced: a dropped cluster write is lost audio,
/// so the caller must see the refusal and retry.
///
/// **Six operations, keyed by handle, except the open, keyed by path.** The offset write serves the recorder's drain;
/// open, read, truncate, close and stats serve its finalize and create, which after boot go through this channel too.
/// An open that fails says why, so the recorder retries its folder only when the folder is missing. A whole file read
/// or written in one operation belongs to @ref file_bytes.h's read-into and write-from.
///
/// **Fixed-width, never a native enum.** `arm-none-eabi` defaults to short enums, so a native enum
/// layout must never be relied on across this boundary.
///
/// **What a collect answers**, in @ref channel.h's vocabulary: @ref DELUGE_CHANNEL_PENDING while the operation is
/// requested or running (ask again on a later tick; do NOT re-request); @ref DELUGE_CHANNEL_READY when it completed,
/// and @ref DELUGE_CHANNEL_FAILED when it did not, both terminal, their answer consumed;
/// @ref DELUGE_CHANNEL_UNKNOWN when nothing is known about it, before its first request included: request it (again).
///
/// **Answers are stamped with the card generation their request was made under.** After an ejection, an answer to
/// a request from before it is never collected, and its collect answers @ref DELUGE_CHANNEL_UNKNOWN.
#ifndef LIBDELUGE_FILE_STREAM_H
#define LIBDELUGE_FILE_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"
#include "channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Ask for `len` bytes at `bytes` to be written to `handle` at absolute `offset`. [task]
///
/// @param handle A stream handle from @ref deluge_file_stream_collect_open or `deluge_efatfs_stream_open`.
///               @ref DELUGE_HANDLE_NONE is refused.
/// @param offset Absolute byte offset within the file.
/// @param bytes  Caller-owned buffer. Must stay alive and unmodified until the matching collect is
///               terminal. NOT copied.
/// @param len    Byte count.
/// @return false when the request was refused -- `handle` already has a write in flight, the
///         channel is not running, or a pointer/length is unusable. The caller MUST retry or take
///         its synchronous arm; nothing has been queued.
bool deluge_file_stream_request_write_at(DelugeHandle handle, uint32_t offset, const uint8_t* bytes,
                                         uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the answer for `handle`'s in-flight write. [task]
///
/// @param handle      The handle passed to @ref deluge_file_stream_request_write_at.
/// @param out_written Written ONLY on @ref DELUGE_CHANNEL_READY, with the true byte count. A
///                    count below the requested `len` is a short write; the caller treats it as a
///                    card error.
/// @return one of the four statuses above.
DelugeChannelStatus deluge_file_stream_collect_write_at(DelugeHandle handle, uint32_t* out_written) DELUGE_NOEXCEPT;

/// @brief Ask for `path` to be opened for writing. [task]
///
/// The only PATH-keyed operation here: there is no handle yet. The handle arrives in the collect, which names the
/// same path.
/// @param path NUL-terminated path.
/// @param mode `DelugeStreamMode` as a fixed-width byte. See stream_io.h, which is authoritative:
///             0 read, 1 create TRUNCATING an existing file, 2 create NEW and fail if it already
///             exists, 3 append (open existing, no truncation).
///
///             1 and 2 are easy to read backwards -- this comment had them backwards -- so take them
///             from the enum rather than from memory. `create_context`'s parameter is named
///             `exclusive`, and mode 2 is the exclusive one.
/// @return false when refused -- another operation is in flight, or the channel is not running.
bool deluge_file_stream_request_open(const char* path, uint8_t mode) DELUGE_NOEXCEPT;

/// @brief Collect the open of @p path. [task]
/// @param path       The path passed to @ref deluge_file_stream_request_open. An answer for another path is left for
///                   its own caller, and this collect answers @ref DELUGE_CHANNEL_UNKNOWN.
/// @param out_handle Written ONLY on @ref DELUGE_CHANNEL_READY, with a handle that is never
///                   @ref DELUGE_HANDLE_NONE.
/// @param out_error  Written ONLY on @ref DELUGE_CHANNEL_FAILED, with the status the open failed with:
///                   `DELUGE_ERR_NOT_FOUND` for a missing folder, `DELUGE_ERR_NO_FILESYSTEM` when the card could not
///                   be mounted, `DELUGE_ERR_IO` when every stream slot is taken. May be null.
DelugeChannelStatus deluge_file_stream_collect_open(const char* path, DelugeHandle* out_handle,
                                                    DelugeStatus* out_error) DELUGE_NOEXCEPT;

/// @brief Ask for `len` bytes at `offset` to be read into `dst`. [task]
///
/// Goes through the HANDLE, not a path, and that is load-bearing: it reads bounded by the handle's
/// live, not-yet-flushed size (`deluge_efatfs_stream_read_at_via`). A path-keyed read would see the
/// on-disk extent instead, which during finalize is the wrong number.
/// @param handle A stream handle. @ref DELUGE_HANDLE_NONE is refused.
/// @param offset Absolute byte offset within the file.
/// @param dst    Caller-owned destination. Must stay alive and unmodified until the collect is
///               terminal. NOT copied.
/// @param len    Byte count to read.
/// @return false when refused; the caller must retry or take its synchronous arm.
bool deluge_file_stream_request_read_at(DelugeHandle handle, uint32_t offset, uint8_t* dst,
                                        uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the read. [task]
/// @param handle   The handle passed to @ref deluge_file_stream_request_read_at.
/// @param out_read Written ONLY on @ref DELUGE_CHANNEL_READY, with the TRUE byte count, which may
///                 be short of `len` at EOF. The caller decides whether short is an error; for the
///                 recorder mid-transform it is.
DelugeChannelStatus deluge_file_stream_collect_read_at(DelugeHandle handle, uint32_t* out_read) DELUGE_NOEXCEPT;

/// @brief Ask for the file behind `handle` to be truncated to `size` bytes. [task]
/// @return false when refused.
bool deluge_file_stream_request_truncate(DelugeHandle handle, uint32_t size) DELUGE_NOEXCEPT;

/// @brief Collect the truncate. [task]
DelugeChannelStatus deluge_file_stream_collect_truncate(DelugeHandle handle) DELUGE_NOEXCEPT;

/// @brief Ask for `handle` to be closed, flushing its deferred directory entry. [task]
///
/// A caller that will DELETE this file must wait for the collect to be terminal before unlinking.
/// Closing flushes the size/first_cluster/mtime edit deferred for the whole recording; unlinking
/// first leaves a stale near-empty entry with `first_cluster` still ~0, frees no clusters, and lets a
/// later file that recycles the freed short-name slot receive this handle's flush.
/// @return false when refused.
bool deluge_file_stream_request_close(DelugeHandle handle) DELUGE_NOEXCEPT;

/// @brief Collect the close. [task]
DelugeChannelStatus deluge_file_stream_collect_close(DelugeHandle handle) DELUGE_NOEXCEPT;

/// @brief Ask for the filesystem's free-cluster count. [task]
///
/// Exists for the recorder's pre-flight space check, and doubles as the only instrument that can see
/// a leaked cluster chain -- an ordering bug that deletes a file before closing it is invisible to
/// any byte comparison, because the file is gone either way.
/// @return false when refused.
bool deluge_file_stream_request_stats(void) DELUGE_NOEXCEPT;

/// @brief Collect the free-cluster count. [task]
/// @param out_free_clusters Written ONLY on @ref DELUGE_CHANNEL_READY.
DelugeChannelStatus deluge_file_stream_collect_stats(uint32_t* out_free_clusters) DELUGE_NOEXCEPT;

/// @brief Whether a queued or running operation reads or writes through @p buffer. [task]
///
/// The offset read and write take caller-owned buffers by pointer and cannot be cancelled, so a caller that gives up
/// on one must not free or reuse its buffer while this is true. Answers true when it cannot tell.
///
/// @param buffer The buffer handed to a request, as passed (its start address).
bool deluge_file_stream_holds(const void* buffer) DELUGE_NOEXCEPT;

#ifdef DELUGE_HOST
/// @brief Host harness only: make every stream-channel operation wait @p ms milliseconds before it touches the card.
/// [task]
///
/// Lets a gate hold an operation in flight while a consumer does something else. 0 (the default) means no wait.
/// Declared only for host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per operation.
void deluge_file_stream_set_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;

/// @brief Host harness only: while @p refused, an open this channel runs on an unmounted card fails with
///        `DELUGE_ERR_NO_FILESYSTEM` instead of mounting it. [task]
///
/// Lets a gate fail an open with a status other than `DELUGE_ERR_NOT_FOUND`. Other channels still mount. Declared only
/// for host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param refused Whether the mount is refused. false (the default) mounts as the device does.
void deluge_file_stream_set_mount_refused(bool refused) DELUGE_NOEXCEPT;
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIBDELUGE_FILE_STREAM_H
