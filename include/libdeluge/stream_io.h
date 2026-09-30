/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// libdeluge/stream_io.h — real-time Sample audio streaming (playback read,
/// recording write).
///
/// Scoped specifically to `Sample` cluster streaming (`SampleStream`'s read
/// source for playback, `SampleRecorder` for the write side) -- not a general
/// file API. `WaveTable`/presets/songs stay on `file_io.h`. See
/// docs/superpowers/specs/2026-07-15-deluge-stream-boundary-design.md.
///
/// The whole boundary is the `deluge_efatfs_stream_*` C-ABI below, backing
/// `deluge::io::Stream` (stream.hpp/.cpp): a persistent write handle held
/// open across a whole recording, `write_at`/`read_at_via` positional but
/// never sparse (`byte_offset` may be anywhere at or below the stream's
/// current end of file, never past it -- callers append whole clusters in
/// increasing index order, then rewrite already-written bytes in place via
/// `SampleRecorder`'s finalize header patch and `alterFile`'s cluster
/// rewrites), and an EOF-honest `read_at_via` that is NOT the streaming read
/// path (that goes through embedded-fatfs, `deluge_efatfs_read_at`) -- it is
/// only the read-BACK of a stream this boundary is itself writing.
#ifndef LIBDELUGE_STREAM_IO_H
#define LIBDELUGE_STREAM_IO_H

#include "abi.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Mode for deluge_efatfs_stream_open: what the stream is opened for.
typedef enum DelugeStreamMode : uint8_t {
	DELUGE_STREAM_READ,             ///< open an existing file (no write/read_at_via use)
	DELUGE_STREAM_WRITE_CREATE,     ///< create the file, truncating if it exists
	DELUGE_STREAM_WRITE_CREATE_NEW, ///< create the file; fails if it already exists
	DELUGE_STREAM_WRITE_APPEND,     ///< open an existing file for writing at its current size; no truncation
} DelugeStreamMode;

/// The embedded-fatfs persistent stream-write C-ABI --
/// the whole of this boundary; `deluge::io::Stream` (stream.cpp) routes every
/// recorder write through these. Implemented in `efatfs_fs.rs` (device) /
/// `efatfs_host_shim.rs` (host), both composing the storage-generic
/// `efatfs_core::write_context_noflush`/`flush_context`/`read_at_via_context`
/// primitives -- see those files' doc comments for the handle-table/locking
/// discipline.
///
/// The handle holds ONE persistent `FileContext` across the life of a
/// recording, keeping the file's in-memory size/mtime edit resident between
/// writes and only persisting it to the on-disk directory entry on
/// `deluge_efatfs_stream_close` (see `write_context_noflush`'s
/// doc). Every handle is a @ref DelugeHandle: 0 is none, and a call given it
/// fails as it fails any handle that names no open stream.
///
/// Each returns a @ref DelugeStatus. `DELUGE_ERR_PARAM` is a call no stream could serve: a null
/// pointer, or the handle 0. Past the open, the efatfs layer keeps no finer reason than "the operation
/// failed", so a handle that names no open stream, an unmounted volume and a failed card access are
/// all `DELUGE_ERR_IO`.

/// @brief Open a persistent stream-write handle. [task]
///
/// @param path       Forward-slash path rooted at the storage volume.
/// @param mode       A `DelugeStreamMode` value (0=READ, 1=WRITE_CREATE, 2=WRITE_CREATE_NEW,
///                   3=WRITE_APPEND).
/// @param out_handle Set to the opened handle on success; untouched on failure.
/// @return `DELUGE_OK`; `DELUGE_ERR_PARAM` for a null or non-UTF-8 path, a null @p out_handle or an
///         unknown @p mode; `DELUGE_ERR_NO_FILESYSTEM` if no volume is mounted; the open's own status
///         (`DELUGE_ERR_NOT_FOUND`, `DELUGE_ERR_EXISTS`, `DELUGE_ERR_NO_SPACE`, ...) if it fails;
///         `DELUGE_ERR_IO` if every stream handle is in use.
DelugeStatus deluge_efatfs_stream_open(const char* path, uint8_t mode, DelugeHandle* out_handle) DELUGE_NOEXCEPT;

/// @brief Write `count` bytes at absolute `byte_offset`, advancing the handle's persisted
///        `FileContext` by the bytes actually written (`*out_written`). Does NOT persist the
///        advanced size/mtime to the on-disk directory entry -- see `deluge_efatfs_stream_close`. [task]
/// @return `DELUGE_OK` (`*out_written` set); `DELUGE_ERR_PARAM` for the handle 0 or a null pointer;
///         `DELUGE_ERR_IO` otherwise.
DelugeStatus deluge_efatfs_stream_write_at(DelugeHandle handle, uint32_t byte_offset, const void* src, uint32_t count,
                                           uint32_t* out_written) DELUGE_NOEXCEPT;

/// @brief EOF-honest read at absolute `byte_offset`, bounded by the handle's IN-MEMORY size (i.e.
///        this can read back data written earlier in the same unflushed session). `*out_read` is
///        the TRUE byte count actually read (`<= count`, less at EOF), never zero-padded. [task]
/// @return `DELUGE_OK` (`*out_read` set); `DELUGE_ERR_PARAM` for the handle 0 or a null pointer;
///         `DELUGE_ERR_IO` otherwise.
DelugeStatus deluge_efatfs_stream_read_at_via(DelugeHandle handle, uint32_t byte_offset, void* dst, uint32_t count,
                                              uint32_t* out_read) DELUGE_NOEXCEPT;

/// @brief Truncate the file behind `handle` to `new_len` bytes. [task]
/// @return `DELUGE_OK`; `DELUGE_ERR_PARAM` for the handle 0; `DELUGE_ERR_IO` otherwise.
DelugeStatus deluge_efatfs_stream_truncate(DelugeHandle handle, uint32_t new_len) DELUGE_NOEXCEPT;

/// @brief Size of the file behind `handle`, in bytes. [task]
/// @return `DELUGE_OK` (`*out_size` set); `DELUGE_ERR_PARAM` for the handle 0 or a null @p out_size;
///         `DELUGE_ERR_IO` otherwise.
DelugeStatus deluge_efatfs_stream_size(DelugeHandle handle, uint32_t* out_size) DELUGE_NOEXCEPT;

/// @brief Persist the handle's accumulated in-memory size/mtime edit (built up by prior
///        `deluge_efatfs_stream_write_at` calls) to the on-disk directory entry, and close a
///        persistent stream-write handle opened via `deluge_efatfs_stream_open`, freeing its slot.
///        The slot is freed whatever the status. [task]
/// @return `DELUGE_OK`; `DELUGE_ERR_PARAM` for the handle 0; `DELUGE_ERR_IO` if the flush failed.
DelugeStatus deluge_efatfs_stream_close(DelugeHandle handle) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_STREAM_IO_H
