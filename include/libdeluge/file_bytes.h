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
/// @brief A WHOLE small file, read or written over the C ABI, without holding the caller.
///
/// Request/collect, the same non-blocking shape @ref audio_load.h, `streaming_fill.h`,
/// @ref file_prefix.h and @ref file_directory.h already use. It exists for the settings family --
/// runtime feature settings, the MIDI device table, MIDI follow's defaults, performance view -- whose
/// reads and writes today run synchronously on whichever tier touched a menu, and the interaction
/// tier may never wait on a lower one.
///
/// **Whole-file, because these files are whole-file objects.** Nothing in the settings family
/// streams: each is serialised in one pass into memory and parsed in one pass out of it. So the
/// boundary carries bytes, not a handle, and the caller never holds an open file across a tick.
/// Every line of XML parsing, policy and migration stays on the C++ side; only the source and the
/// sink move.
///
/// **One operation at a time, and answers are keyed by kind as well as by path.** A read answer can
/// never be mistaken for a write answer even when both name the same file. Single-flight rather than
/// a queue because the filesystem beneath serialises everything anyway, and because it is what makes
/// the refusal below observable: a caller that is told "no" knows its intent did not happen.
///
/// **Reads coalesce; writes do NOT.** A second @ref deluge_file_bytes_request_read for a path already
/// in flight is a no-op, because re-asking a question already asked costs nothing and the collect
/// reports the one answer. A @ref deluge_file_bytes_request_write that arrives while the channel is
/// busy is REFUSED -- it returns false and nothing is queued or replaced. Two rapid toggles of a setting
/// are two distinct intents, and superseding the first would silently lose a settings change; a
/// dropped directory listing is merely re-asked, but a dropped write is gone. The caller must retry a
/// refused write, and this is the one place this channel deliberately differs from
/// @ref file_directory.h.
///
/// **A write creates its parent directory.** That is why there is no `mkdir` operation here: every
/// settings caller's `mkdir SETTINGS_FOLDER` existed only to make its own write succeed, and a card
/// WRITE is no more allowed on the interaction tier than a read is.
///
/// **Fixed-width, never a native enum.** Every value crossing this boundary is a fixed-width
/// integer, for the reason @ref audio_load.h gives: `arm-none-eabi` defaults to short enums, so a
/// native enum layout must never be relied on across it. `DelugeStatus` is itself declared with a
/// fixed underlying type (`int8_t`), which is why it may appear here at all.
#ifndef LIBDELUGE_FILE_BYTES_H
#define LIBDELUGE_FILE_BYTES_H

#include "libdeluge/types.h"

#include <stdbool.h>
#include <stdint.h>

/* Not currently parsed as C -- same situation as audio_load.h, file_prefix.h and file_directory.h,
 * and the same forward-looking hardening: `noexcept` is C++-only and invalid C syntax, so it is
 * spelled through this macro (#undef'd at the end of the file so it cannot leak into, or collide
 * with a same-named macro in, whatever includes this header next) rather than written bare. */
#ifdef __cplusplus
#define DELUGE_NOEXCEPT noexcept
extern "C" {
#else
#define DELUGE_NOEXCEPT
#endif

/// @brief The longest path this channel can key on, INCLUDING the NUL — so the text it holds is one
///        byte shorter.
///
/// Spelled here rather than left to `audio_load.h`'s identically-valued `DELUGE_MAX_PATH`, for the
/// reason @ref file_directory.h gives: a caller needs it to decide whether a path is servable at all
/// and should not have to include an unrelated header to find out.
///
/// @note A longer path is REFUSED, never truncated — a truncated path names a *different* file, which
///       may well exist and would then be read or, far worse, overwritten.
#define DELUGE_FILE_BYTES_MAX_PATH 256u

/// @brief The largest file this channel will carry, in bytes.
///
/// Set by measurement, in both directions. Real settings files on a real card measure 1.4-2.0 KiB
/// (`CommunityFeatures.XML` 1453 B, `MIDIFollow.XML` 1957 B), so this is roughly 4x the largest one
/// that exists. It is deliberately NOT `FileWriter`'s 32 KiB buffer, which is what bounds the
/// serialiser: the Rust side stages one of these for a pending request and one for an uncollected
/// answer, and at 32 KiB each the resulting 64 KiB of on-chip `.bss` pushes `.ARM.exidx` into
/// `.rtt_cached_reserve` and the device image stops linking. 16 KiB was measured to still link, so
/// there is headroom -- but raising this is a device-link question, not a free change.
///
/// @note A file larger than this is never truncated. The read reports @ref DELUGE_FILE_BYTES_FAILED
///       with `DELUGE_ERR_NO_MEMORY` and the write is refused, because a truncated settings file
///       parsed as if whole would silently reset every setting past the cut.
#define DELUGE_FILE_BYTES_MAX_BYTES 8192u

/// @brief The largest single transfer this channel's offset-addressed operations will carry.
///
/// Equal to `smsysex`'s `blockBufferMax` (`smsysex.cpp:34`), which is the only consumer: one SysEx
/// message carries at most this many bytes, so a larger block could never be filled.
///
/// Deliberately much smaller than @ref DELUGE_FILE_BYTES_MAX_BYTES, and that is a device-link
/// constraint rather than a preference. This bound sizes a THIRD staging buffer, and two 8 KiB ones
/// already sit in the Rust side; at 32 KiB each they pushed `.ARM.exidx` into `.rtt_cached_reserve`
/// and the image stopped linking. Keep this one small.
#define DELUGE_FILE_BYTES_BLOCK 1024u

/// @brief Collect: requested, or being read/written right now.
///
/// Ask again on a later tick. Do NOT re-request: the operation is already under way.
#define DELUGE_FILE_BYTES_PENDING 0u

/// @brief Collect: the operation completed, and its answer was removed from the slot.
///
/// For a read, `out_buf` now holds the file's bytes and `out_len` their count. For a write or a
/// rename, the bytes reached the card. Terminal: the answer has been consumed, so a second collect
/// for the same path reports @ref DELUGE_FILE_BYTES_UNKNOWN.
#define DELUGE_FILE_BYTES_READY 1u

/// @brief Collect: nothing is known about this path. Request it (again).
///
/// This is also what a caller sees before its first request, after a card swap discarded an answer
/// read from a card that has gone, and when a *different* path currently occupies the slot.
#define DELUGE_FILE_BYTES_UNKNOWN 2u

/// @brief Collect: the operation did not happen. `out_status` says why.
///
/// TERMINAL -- do NOT re-request, or a file that cannot be read is re-requested every tick forever.
/// Carries a status because the caller distinguishes "no such file" (`DELUGE_ERR_NOT_FOUND`: write
/// the defaults) from "the card is unreadable" (`DELUGE_ERR_IO`: write nothing, or the user's real
/// settings are replaced by defaults) -- the same distinction `existence_policy.h`'s `Presence`
/// exists to preserve.
#define DELUGE_FILE_BYTES_FAILED 3u

/// @brief Ask for the whole of `path` to be read, and return immediately.
///
/// A request for a path already in flight is a no-op rather than a replacement, so a caller polling
/// each tick cannot starve its own read. Silently drops an unusable request (NULL, over long, or a
/// slot already holding a different path): there is no failure a caller could act on that
/// re-requesting does not already cover, and the collect it runs next tick reports
/// @ref DELUGE_FILE_BYTES_UNKNOWN, whose documented meaning is exactly that.
///
/// @param path NUL-terminated path of the file to read. Copied out before this returns.
void deluge_file_bytes_request_read(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the bytes read for `path`, if they are ready.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_buf    Written **only** on @ref DELUGE_FILE_BYTES_READY, with `*out_len` bytes. No NUL
///                   is appended: a caller that wants one must reserve the room itself.
/// @param buf_cap    Capacity of `out_buf`. A file longer than this reports
///                   @ref DELUGE_FILE_BYTES_FAILED with `DELUGE_ERR_NO_MEMORY` rather than a
///                   truncated read, because a settings file cut short parses as a file whose
///                   remaining settings were absent.
/// @param out_len    Written **only** on @ref DELUGE_FILE_BYTES_READY, with the byte count. Untouched
///                   on every other status, so it may be passed uninitialised.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED, with the status that stopped
///                   the read. Untouched on every other status.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
/// @note READY and FAILED are both terminal: the answer is removed as it is reported, so asking a
///       second time reports UNKNOWN. UNKNOWN alone means "request it (again)".
uint8_t deluge_file_bytes_collect_read(const char* path, uint8_t* out_buf, uint32_t buf_cap, uint32_t* out_len,
                                       DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `len` bytes to be written over `path`, and return immediately.
///
/// Creates `path`'s parent directory if it is absent, so a caller needs no `mkdir` of its own.
///
/// @param path  NUL-terminated destination path. Copied out before this returns.
/// @param bytes The bytes to write. **Copied out before this returns**, so the caller's buffer is
///              free the instant this call ends and a serialiser may be destroyed immediately after.
///              Borrowing instead would be cheaper by one buffer and would put a lifetime rule at
///              every call site — and a caller doing something after the call returns is the exact
///              shape of defect this ladder has produced repeatedly.
/// @param len   Byte count, at most @ref DELUGE_FILE_BYTES_MAX_BYTES. A longer write is REFUSED
///              rather than clipped, because a settings file cut short parses as one whose remaining
///              settings were absent.
/// @return true if the write was accepted and a collect will eventually report it. false if it was
///         REFUSED -- the channel is busy with another operation, the path is NULL or over long, or
///         `len` exceeds @ref DELUGE_FILE_BYTES_MAX_BYTES. **A refused write did not happen and
///         nothing is queued**, so the caller must keep its intent and retry; unlike a read, a lost
///         request here is a lost settings change rather than a question worth re-asking.
bool deluge_file_bytes_request_write(const char* path, const uint8_t* bytes, uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the write to `path`, if it has finished.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED. READY means the bytes
///         reached the card and the caller's buffer is free again.
uint8_t deluge_file_bytes_collect_write(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `from` to be renamed to `to`, and return immediately.
///
/// For the settings family's legacy-path migration, which moves a file left at the card root into
/// `SETTINGS/`. Keyed on `to`, because that is the name the caller goes on to use.
///
/// @param from NUL-terminated source path. Copied out before this returns.
/// @param to   NUL-terminated destination path. Copied out before this returns.
/// @return true if accepted. false if REFUSED -- the channel is busy with another operation, or
///         either path is NULL or over long. Refused like a write rather than dropped like a read: a migration that
///         silently did not happen leaves the file where the next boot will migrate it again.
bool deluge_file_bytes_request_rename(const char* from, const char* to) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the rename to `to`, if it has finished.
///
/// @param to         NUL-terminated destination path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
/// @note A rename whose source does not exist reports FAILED with `DELUGE_ERR_NOT_FOUND`. For the
///       migration callers that is the ordinary case -- there is no legacy file -- and not an error
///       to show anyone.
uint8_t deluge_file_bytes_collect_rename(const char* to, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path` to be deleted, and return immediately.
///
/// @param path NUL-terminated path. Copied out before this returns.
/// @return true if accepted. false if REFUSED -- the channel is busy with another operation, or the
///         path is NULL or over long. Refused like a write rather than dropped like a read, because a
///         caller that believes a file is gone when it is not will act on that belief.
bool deluge_file_bytes_request_unlink(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the deletion of `path`, if it has finished.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
/// @note Deleting a path that was already gone reports FAILED with `DELUGE_ERR_NOT_FOUND`. For the
///       startup fail-safe's cleanup that is success by another name -- the file is not there, which
///       is what was wanted -- so that caller must not treat it as an error.
uint8_t deluge_file_bytes_collect_unlink(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask whether `path` exists, and return immediately.
///
/// **Not a read.** Answering existence by requesting @ref deluge_file_bytes_request_read and reading
/// `NOT_FOUND` as absence works only for files below @ref DELUGE_FILE_BYTES_MAX_BYTES: a larger file
/// that IS there reports FAILED with `DELUGE_ERR_NO_MEMORY`, and a caller collapsing the two would
/// read "present" as "absent" and may then overwrite it. Song files are far above that bound, which
/// is why this operation exists rather than being expressed in terms of the read.
///
/// A request for a path already in flight is a no-op rather than a replacement, exactly as the read's
/// is, and an unusable request is dropped for the same reason: the next collect reports UNKNOWN,
/// whose documented meaning is "ask again".
///
/// @param path NUL-terminated path. Copied out before this returns.
void deluge_file_bytes_request_exists(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the existence answer for `path`, if there is one.
///
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_present Written **only** on @ref DELUGE_FILE_BYTES_READY: true if the path is there,
///                    false if it is known not to be. Untouched on every other status, so it may be
///                    passed uninitialised.
/// @param out_status  Written **only** on @ref DELUGE_FILE_BYTES_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
/// @note THREE-STATE, and the third state is the point. READY with `*out_present == false` means
///       "known absent"; FAILED means "could not establish", which is NOT absence. A caller that
///       collapses FAILED into "absent" will write over files that are still there -- see
///       `existence_policy.h`'s `StartupPresence`, which is this distinction given a name.
uint8_t deluge_file_bytes_collect_exists(const char* path, bool* out_present, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask whether `path` exists as a DIRECTORY, and return immediately.
///
/// @ref deluge_file_bytes_request_exists opens its path as a file, which a directory is not, so it answers FAILED for
/// every directory. This one opens the path as a directory and closes it again before answering, so no directory
/// handle is held past the answer. Otherwise it behaves as that request: a request for a path already in flight is a
/// no-op, and an unusable request is dropped.
///
/// @param path NUL-terminated path. Copied out before this returns.
void deluge_file_bytes_request_dir_exists(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the directory-existence answer for `path`, if there is one.
///
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_present Written **only** on @ref DELUGE_FILE_BYTES_READY: true if the directory is there, false if it is
///                    known not to be. Untouched on every other status.
/// @param out_status  Written **only** on @ref DELUGE_FILE_BYTES_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
/// @note Three-state, exactly as @ref deluge_file_bytes_collect_exists: FAILED is "could not establish", which is NOT
///       absence.
uint8_t deluge_file_bytes_collect_dir_exists(const char* path, bool* out_present,
                                             DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path` and every missing directory above it to be created, and return immediately.
///
/// Whole-path, not one segment. A caller that walked segments itself would need a phase machine per
/// caller to do it over this channel, and both callers that need this already have a segment loop
/// that this deletes. It is also the shape the channel already has: @ref
/// deluge_file_bytes_request_write creates its destination's parent directory, so segment arithmetic
/// is below this boundary already.
///
/// @param path NUL-terminated path of the deepest directory wanted. Copied out before this returns.
/// @return true if accepted. false if REFUSED -- the channel is busy with another operation, or the
///         path is NULL or over long.
/// @note An already-existing path is SUCCESS, not `DELUGE_ERR_EXISTS`. Callers use this to mean "make
///       sure this is there", and the commonest outcome is that it already is.
bool deluge_file_bytes_request_mkdir_all(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the directory creation for `path`, if it has finished.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED, with the first failure that
///                   was not "already exists". Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
uint8_t deluge_file_bytes_collect_mkdir_all(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path`'s modification time to be set, and return immediately.
///
/// Its own operation rather than an optional parameter on @ref deluge_file_bytes_request_mkdir_all,
/// because only two of its four callers follow a directory creation: the other two stamp an arbitrary
/// path on request and stamp a file after writing it.
///
/// @param path      NUL-terminated path. Copied out before this returns.
/// @param timestamp Passed BY VALUE. `DelugeTimestamp` is a fixed-width POD (`types.h`), so it
///                  crosses this boundary without a pointer and without a lifetime rule.
/// @return true if accepted. false if REFUSED -- the channel is busy, or the path is NULL or over
///         long.
bool deluge_file_bytes_request_set_time(const char* path, DelugeTimestamp timestamp) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the timestamp write for `path`, if it has finished.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_FILE_BYTES_PENDING, @ref DELUGE_FILE_BYTES_READY,
///         @ref DELUGE_FILE_BYTES_UNKNOWN, @ref DELUGE_FILE_BYTES_FAILED.
uint8_t deluge_file_bytes_collect_set_time(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for up to @ref DELUGE_FILE_BYTES_BLOCK bytes to be read from `path` at `offset`.
///
/// Stateless: the path and offset are the whole request, and no handle is opened, held or returned.
/// `smsysex`'s `readBlock` already seeks to an address carried on the wire before every read, so this
/// matches its consumer exactly.
///
/// @param path   NUL-terminated path. Copied out before this returns.
/// @param offset Byte offset to read from. Past end-of-file yields zero bytes, not an error.
/// @param len    How many bytes to read, at most @ref DELUGE_FILE_BYTES_BLOCK.
/// @return true if accepted. false if REFUSED -- the channel is busy, the path is NULL or over long,
///         or `len` exceeds the block bound.
bool deluge_file_bytes_request_read_at(const char* path, uint32_t offset, uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the bytes read for `path`, if they are ready.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_buf    Written **only** on @ref DELUGE_FILE_BYTES_READY, with `*out_len` bytes.
/// @param buf_cap    Capacity of `out_buf`. A read longer than this reports
///                   @ref DELUGE_FILE_BYTES_FAILED with `DELUGE_ERR_NO_MEMORY`.
/// @param out_len    Written **only** on READY. **A short count is not an error**: it means
///                   end-of-file was reached, which is how a caller detects it.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED.
uint8_t deluge_file_bytes_collect_read_at(const char* path, uint8_t* out_buf, uint32_t buf_cap, uint32_t* out_len,
                                          DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `len` bytes to be written into `path` at `offset`.
///
/// @warning **This does NOT truncate, and must not.** `efatfs_core::create_context` truncates
///          unconditionally, so an implementation that re-opened with `DELUGE_FILE_WRITE_CREATE`
///          would erase the file on every block and a multi-block transfer would end holding only
///          its last block. The Rust side opens non-truncating; a caller that wants an empty file
///          asks for one explicitly with @ref deluge_file_bytes_request_write and zero bytes, which
///          is exactly what `smsysex`'s open-for-write does.
///
/// @param path   NUL-terminated destination. Created if absent -- an absent file is empty, so
///               truncation is moot on that path alone.
/// @param offset Byte offset to write at.
/// @param bytes  Copied out before this returns.
/// @param len    Byte count, at most @ref DELUGE_FILE_BYTES_BLOCK.
/// @return true if accepted. false if REFUSED.
bool deluge_file_bytes_request_write_at(const char* path, uint32_t offset, const uint8_t* bytes,
                                        uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the write to `path`, if it has finished.
///
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_written Written **only** on READY, with the byte count actually written. A short write
///                    is reported honestly rather than as success; the caller decides what it means.
/// @param out_status  Written **only** on FAILED.
uint8_t deluge_file_bytes_collect_write_at(const char* path, uint32_t* out_written,
                                           DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path`'s size.
///
/// Dropped rather than refused on an unusable request, like the other queries: the next collect
/// reports UNKNOWN, whose documented meaning is "ask again".
/// @param path NUL-terminated path. Copied out before this returns.
void deluge_file_bytes_request_stat(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect `path`'s size, if it is known.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_size   Written **only** on @ref DELUGE_FILE_BYTES_READY.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED, with `DELUGE_ERR_NOT_FOUND`
///                   for a path that is not there.
uint8_t deluge_file_bytes_collect_stat(const char* path, uint32_t* out_size, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `from` to be copied to `to`, whole.
///
/// The entire copy, not a block of it: the read/write loop lives on the Rust side so its caller needs
/// neither a loop nor two open files. `to`'s parent directory is created if absent, and `to` is
/// truncated before the copy -- a copy onto a longer existing file must not leave its tail behind.
/// @return true if accepted. false if REFUSED.
bool deluge_file_bytes_request_copy(const char* from, const char* to) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the copy to `to`, if it has finished.
///
/// Keyed on the DESTINATION, because that is the name the caller goes on to use.
/// @param to         NUL-terminated destination, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_FILE_BYTES_FAILED.
uint8_t deluge_file_bytes_collect_copy(const char* to, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Whether the Rust async whole-file channel backs this header on this BSP/config.
/// @return true if a request will actually be served; false if every caller must do its own file I/O,
///         synchronously. When this is false, the request functions do nothing (and the two that
///         return a bool return false) and every collect reports UNKNOWN -- so a caller that only
///         polled would wait forever, and must take its synchronous arm instead.
bool deluge_file_bytes_async_active(void) DELUGE_NOEXCEPT;

#ifdef DELUGE_HOST
/// @brief Host harness only: make every byte-channel operation wait @p ms milliseconds before it touches the card.
///
/// Lets a gate hold an operation in flight while a consumer does something else. 0 (the default) means no wait.
/// Declared only for host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per operation.
void deluge_file_bytes_set_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;
#endif

#ifdef __cplusplus
}
#endif
#undef DELUGE_NOEXCEPT
#endif // LIBDELUGE_FILE_BYTES_H
