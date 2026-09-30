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

/// libdeluge/file_io.h — file-level storage access.
///
/// The application's portable code (storage_manager, audio_file_manager, the
/// browser, the companion SysEx protocol, ...) reads/writes files and
/// directories through this boundary instead of a specific filesystem
/// library. On rza1/host/Embassy this is backed by the vendored FatFS
/// library over `block_device.h`; that dependency is a BSP-internal
/// implementation detail, not something the app links against directly. A
/// BSP may instead back it with the host OS's native filesystem (e.g. a
/// Linux BSP mapping straight to POSIX I/O) with no FatFS involved at all.
///
/// Paths are plain forward-slash strings rooted at the storage volume (no
/// FatFS drive-number prefix, e.g. "SONGS/mysong.XML" not "0:/SONGS/...").
#ifndef LIBDELUGE_FILE_IO_H
#define LIBDELUGE_FILE_IO_H

#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// An open file. Opaque; owned by the BSP implementation.
typedef struct DelugeFile DelugeFile;

/// An open directory iterator. Opaque; owned by the BSP implementation.
typedef struct DelugeDir DelugeDir;

/// Longest filename this boundary will report/accept, including the NUL
/// terminator (FAT LFN max is 255 characters).
#define DELUGE_MAX_FILENAME 256

/// @brief Mode a file is opened in via deluge_efatfs_file_open.
typedef enum DelugeFileOpenMode : uint8_t {
	DELUGE_FILE_READ,             ///< open an existing file for reading
	DELUGE_FILE_WRITE_CREATE,     ///< create the file, truncating if it exists
	DELUGE_FILE_WRITE_CREATE_NEW, ///< create the file; fails with DELUGE_ERR_EXISTS if it already exists
} DelugeFileOpenMode;

/// One directory entry returned by `deluge_efatfs_dir_read`.
typedef struct DelugeDirEntry {
	char name[DELUGE_MAX_FILENAME];
	bool is_directory;
	uint32_t size;
	DelugeTimestamp modified_time;
	bool is_read_only;
	bool is_hidden;
	bool is_system;
	bool is_archive;
} DelugeDirEntry;

/// The embedded-fatfs (Rust `efatfs_streaming`) task-context file/directory
/// C-ABI. `deluge::io::File`/`Directory` (file.cpp) route to these
/// unconditionally. Implemented in
/// `efatfs_fs.rs` (device) / `efatfs_host_shim.rs` (host), both composing the
/// storage-generic `efatfs_core` primitives -- see those files' doc comments
/// for the handle-table/locking discipline. Every handle-taking function's
/// handle is an opaque `u32` boxed into the corresponding
/// `DelugeFile*`/`DelugeDir*` pointer (cast, not dereferenced), the same
/// convention the streaming handle uses.
///
/// These 11 fallible file/dir/path operations return `DelugeStatus`, so
/// callers (chiefly `file.cpp`) can report a granular error
/// (`DELUGE_ERR_NOT_FOUND`, `DELUGE_ERR_EXISTS`, `DELUGE_ERR_NOT_EMPTY`, ...)
/// instead of a single undifferentiated failure. `_file_close`/`_dir_close`
/// cannot fail and stay `void`. The remaining whole-volume queries below
/// (`_stats`/`_mount`/`_remount`/`_cluster_size`) stay plain `bool`: each has
/// exactly one failure mode (no volume mounted, or the mount/query itself
/// failing), so there is no granular error to distinguish and a `DelugeStatus`
/// would carry no more information than the `bool` already does.

/// @brief Open a task-context file.
///
/// @param path       Forward-slash path rooted at the storage volume.
/// @param mode       Matches `DelugeFileOpenMode`'s declaration-order values
///                    (0=READ, 1=WRITE_CREATE, 2=WRITE_CREATE_NEW).
/// @param out_handle Set to the opened handle on success.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_file_open(const char* path, uint8_t mode, uint32_t* out_handle);

/// @brief EOF-honest read at the handle's current position -- the efatfs
///        backend for `deluge::io::File::read`.
///
/// @param handle   Handle from deluge_efatfs_file_open.
/// @param dst      Destination buffer, at least `count` bytes.
/// @param count    Number of bytes requested.
/// @param out_read Set to the TRUE byte count actually read (`<= count`, less
///                  at real EOF), never zero-padded.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_file_read_exact(uint32_t handle, void* dst, uint32_t count, uint32_t* out_read);

/// @brief Write `count` bytes at the handle's current position, advancing it
///        by the bytes actually written.
///
/// @param handle      Handle from deluge_efatfs_file_open.
/// @param src         Source buffer, at least `count` bytes.
/// @param count       Number of bytes to write.
/// @param out_written Set to the number of bytes actually written.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_file_write(uint32_t handle, const void* src, uint32_t count, uint32_t* out_written);

/// @brief Move the handle's cursor to an absolute byte offset.
///
/// @param handle Handle from deluge_efatfs_file_open.
/// @param offset Absolute byte offset.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_file_seek(uint32_t handle, uint32_t offset);

/// @brief Total size of the file behind `handle`, in bytes.
///
/// @param handle   Handle from deluge_efatfs_file_open.
/// @param out_size Set to the file size on success.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_file_size(uint32_t handle, uint32_t* out_size);

/// @brief Free + total cluster counts of the mounted volume (whole-FS query).
/// @param out_free_clusters  Set to the free cluster count on success.
/// @param out_total_clusters Set to the total cluster count on success.
/// @return true on success (both out-params written); false if no volume is mounted or a query
///         error occurs — in which case the out-params are left untouched.
bool deluge_efatfs_stats(uint32_t* out_free_clusters, uint32_t* out_total_clusters);

/// @brief Mount the storage volume if it isn't already mounted. Idempotent: a call while already
///        mounted is a no-op success.
/// @return true if mounted (or already was); false on mount failure.
bool deluge_efatfs_mount(void);

/// @brief How many times the filesystem has become mounted, as a monotonic counter.
///
/// Read this to notice an unmounted→mounted EDGE. The directory channel's driver mounts on the
/// storage tier — mounting is card I/O and the directory channel's caller is on the interaction
/// tier — so more than one caller can mount, and a one-off before/after sample taken around your
/// own @ref deluge_efatfs_mount call would miss an edge caused by someone else's mount. Persisting
/// the last-seen generation across calls, instead of re-sampling fresh each time, catches the edge
/// no matter which caller mounted.
///
/// A counter rather than a flag, so no reader consumes the edge out from under another.
/// @return A value that changes on every mount. Only changes are meaningful; the absolute value is
///         not, and it may be 0 on a BSP with no filesystem at all.
uint32_t deluge_efatfs_mount_generation(void);

/// @brief Drop the mounted volume and re-mount fresh, invalidating every outstanding handle
///        (streaming reads, task-context files/dirs, and in-progress stream-writes all become
///        invalid — callers must reopen). Used to pick up a card SWAP.
/// @return true on success; false on mount failure.
bool deluge_efatfs_remount(void);

/// @brief Drop the filesystem, leaving it UNMOUNTED.
///
/// The symmetric counterpart of @ref deluge_efatfs_mount, which until now had none —
/// @ref deluge_efatfs_remount drops and re-mounts in one step, so nothing could leave the filesystem
/// unmounted. That is the state a lazily-mounting caller has to be able to start from, so without
/// this it could not be exercised.
void deluge_efatfs_unmount(void);

/// @brief The mounted volume's cluster (allocation-unit) size, in bytes.
/// @param out_bytes Set to the cluster size on success.
/// @return true on success; false if no volume is mounted or a query error
///         occurs — in which case `*out_bytes` is left untouched.
bool deluge_efatfs_cluster_size(uint32_t* out_bytes);

/// @brief Close a task-context file handle opened via deluge_efatfs_file_open.
///
/// @param handle Handle to close.
void deluge_efatfs_file_close(uint32_t handle);

/// @brief Open `path` as a directory for iteration.
///
/// @param path       Forward-slash path rooted at the storage volume.
/// @param out_handle Set to the opened handle on success.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_dir_open(const char* path, uint32_t* out_handle);

/// @brief Read the next directory entry's fields into the caller's out-params
///        (mirrors `DelugeDirEntry`'s fields). If there are no more entries,
///        `*out_has_entry` is set to `false` and the call still returns
///        `DELUGE_OK` (end of directory is not an error). An
///        entry whose name doesn't fit in DELUGE_MAX_FILENAME bytes
///        (including the NUL) is skipped internally -- never truncated into
///        `out_name` -- so this never fails the whole enumeration over one
///        oversized filename.
///
/// @param handle        Handle from deluge_efatfs_dir_open.
/// @param out_name      NUL-terminated on success; must point at
///                       DELUGE_MAX_FILENAME writable bytes.
/// @param out_name_cap  Capacity of `out_name`, in bytes.
/// @param out_is_dir    Set to whether the entry is a directory.
/// @param out_size      Set to the entry's size in bytes.
/// @param out_modified  Set to the packed FAT date/time:
///                       `(dos_date << 16) | dos_time`, the same convention
///                       deluge_efatfs_set_time packs.
/// @param out_attrs     Set to the raw FAT attribute byte (`RDO`=0x01,
///                       `HID`=0x02, `SYS`=0x04, `DIR`=0x10, `ARC`=0x20).
/// @param out_has_entry Set to `false` once enumeration is exhausted.
/// @return `DELUGE_OK` on success (including end-of-directory).
DelugeStatus deluge_efatfs_dir_read(uint32_t handle, char* out_name, uint32_t out_name_cap, bool* out_is_dir,
                                    uint32_t* out_size, uint32_t* out_modified, uint8_t* out_attrs,
                                    bool* out_has_entry);

/// @brief Close a directory handle opened via deluge_efatfs_dir_open.
///
/// @param handle Handle to close.
void deluge_efatfs_dir_close(uint32_t handle);

/// @brief Create a directory.
///
/// @param path Forward-slash path rooted at the storage volume.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_mkdir(const char* path);

/// @brief Delete a file or empty directory.
///
/// @param path Forward-slash path rooted at the storage volume.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_unlink(const char* path);

/// @brief Rename/move a file or directory.
///
/// @param old_path Existing path.
/// @param new_path Destination path.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_rename(const char* old_path, const char* new_path);

/// @brief Set a file or directory's last-modified timestamp.
///
/// @param path   Forward-slash path rooted at the storage volume.
/// @param year   Full calendar year (e.g. 2026).
/// @param month  Month, 1-12.
/// @param day    Day of month, 1-31.
/// @param hour   Hour, 0-23.
/// @param minute Minute, 0-59.
/// @param second Second, 0-59.
/// @return `DELUGE_OK` on success.
DelugeStatus deluge_efatfs_set_time(const char* path, uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                                    uint8_t minute, uint8_t second);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_FILE_IO_H
