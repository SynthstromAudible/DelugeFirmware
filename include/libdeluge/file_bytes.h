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
/// Request/collect, the same non-blocking shape the channels in @ref audio_load.h,
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
/// **Every request is refused while the channel is busy; nothing is queued or replaced.** The channel is busy
/// while any of these holds:
/// - a request is already queued (whatever its kind or path);
/// - an operation is in flight;
/// - an uncollected rename, unlink or write-from answer for the current card is waiting for its owner -- see
///   "Collect what you ask for" below;
/// - the channel's state was being updated at the instant of the call (a lock was contended).
///
/// A request that returns `bool` reports the refusal as false; a query that returns nothing (read, exists,
/// dir_exists, stat) is refused silently, and its collect reads @ref DELUGE_CHANNEL_UNKNOWN until it asks again
/// -- or @ref DELUGE_CHANNEL_PENDING while an identical request is the one in flight, which is how a duplicate
/// read coalesces. A refused write is never superseded or replaced: two rapid toggles of a setting are two distinct
/// intents, and a dropped write is gone where a dropped directory listing is merely re-asked. The caller must retry
/// a refused write, and this is the one place this channel deliberately differs from @ref file_directory.h.
///
/// **Waiting on a busy channel.** The channel raises `DELUGE_WAKE_STORAGE` when it finishes or drops an operation,
/// and when an answer that refused someone is taken, so a refused or PENDING caller runs again then rather than
/// polling. It bounds the wait against @ref deluge_file_bytes_progress rather than against the clock alone
/// (`deluge::io::StallBound::tick(uint32_t)`): the channel may legitimately be busy for a long time with someone
/// else's copy, and only a channel whose progress mark has stopped moving is wedged. A wake-driven caller also asks to
/// run at its bound's deadline, so a channel that never wakes it still ends the wait.
///
/// **Collect what you ask for.** A rename, unlink or write-from cannot safely run twice -- a second rename reports
/// NOT_FOUND for a rename that happened, and a second exclusive write finds its own file -- so an answer of one of
/// those kinds is protected: until its owner collects it, the channel refuses every other caller's request, for up to
/// @ref DELUGE_FILE_BYTES_PROTECTED_ANSWER_SPAN_MS from the first request it refuses, after which the owner is taken to
/// have gone and the answer may be displaced. A caller that requests one of these operations must therefore collect
/// it as soon as it is woken (`DELUGE_WAKE_STORAGE`). One that stops holds every other consumer off for that span.
/// Taking an answer that refused someone raises `DELUGE_WAKE_STORAGE`, so a refused caller asks again then. An answer
/// that ages out raises nothing, so a refused caller must also ask again on its own within the span plus a margin,
/// and in any case before it judges its own stall bound. Every other kind's uncollected answer is displaced by the
/// next operation to finish, and its owner reads UNKNOWN and asks again.
///
/// **Two operations borrow the caller's buffer instead of copying.**
/// @ref deluge_file_bytes_request_read_into and @ref deluge_file_bytes_request_write_from carry a whole document,
/// which may run to a megabyte, so the pointer crosses rather than the bytes. The caller keeps the buffer until the
/// collect is terminal and @ref deluge_file_bytes_holds says the channel has let go. Neither replaces a queued
/// request or is replaced by one: while the channel is busy, each is refused.
///
/// **What a collect answers**, in @ref channel.h's vocabulary:
/// - @ref DELUGE_CHANNEL_PENDING: requested, or running. Collect again when woken (`DELUGE_WAKE_STORAGE`) or at the
///   wait's deadline; do NOT re-request.
/// - @ref DELUGE_CHANNEL_READY: the operation completed and its answer was removed from the slot. For a read,
///   `out_buf` holds the file's bytes and `out_len` their count; for a write or a rename, the bytes reached the
///   card. A second collect for the same path answers UNKNOWN.
/// - @ref DELUGE_CHANNEL_UNKNOWN: nothing is known about this path -- before the first request, after a card swap
///   discarded an answer read from the card that has gone, or while a different path occupies the slot. Request it
///   (again).
/// - @ref DELUGE_CHANNEL_FAILED: the operation did not happen, and `out_status` says why. Terminal: do NOT
///   re-request, or a file that cannot be read is re-requested every tick forever. It carries a status because the
///   caller distinguishes "no such file" (`DELUGE_ERR_NOT_FOUND`: write the defaults) from "the card is unreadable"
///   (`DELUGE_ERR_IO`: write nothing, or the user's real settings are replaced by defaults), the distinction
///   `existence_policy.h`'s `Presence` exists to preserve.
///
/// **A path longer than @ref DELUGE_PATH_MAX, NUL included, is refused, never truncated.** A truncated path names a
/// different file, which may well exist and would then be read or, far worse, overwritten.
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

#include "abi.h"
#include "channel.h"

#ifdef __cplusplus
extern "C" {
#endif

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
/// @note A file larger than this is never truncated. The read reports @ref DELUGE_CHANNEL_FAILED
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

/// @brief How long an uncollected rename, unlink or write-from answer may refuse other requests, in milliseconds,
///        counted from the first request it refuses.
///
/// Past it, the answer's owner is taken to have gone and a new request may displace it. Nothing is raised when that
/// happens, so a caller refused by such an answer asks again on its own no later than this plus a margin.
#define DELUGE_FILE_BYTES_PROTECTED_ANSWER_SPAN_MS 100u

/// @brief Ask for the whole of `path` to be read, and return immediately. [task]
///
/// Silently dropped -- refused, with nothing to report it -- when the path is NULL or over long, or while the
/// channel is busy: a request is already queued (this path's own included), an operation is in flight, an
/// uncollected rename, unlink or write-from answer is waiting for its owner, or the channel was mid-update at the
/// instant of the call. There is no failure a caller could act on that re-requesting does not already cover: the
/// collect it runs next reports @ref DELUGE_CHANNEL_PENDING if this path's read is the one in flight, and
/// @ref DELUGE_CHANNEL_UNKNOWN, whose documented meaning is "ask again", otherwise.
///
/// @param path NUL-terminated path of the file to read. Copied out before this returns.
void deluge_file_bytes_request_read(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the bytes read for `path`, if they are ready. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_buf    Written **only** on @ref DELUGE_CHANNEL_READY, with `*out_len` bytes. No NUL
///                   is appended: a caller that wants one must reserve the room itself.
/// @param buf_cap    Capacity of `out_buf`. A file longer than this reports
///                   @ref DELUGE_CHANNEL_FAILED with `DELUGE_ERR_NO_MEMORY` rather than a
///                   truncated read, because a settings file cut short parses as a file whose
///                   remaining settings were absent.
/// @param out_len    Written **only** on @ref DELUGE_CHANNEL_READY, with the byte count. Untouched
///                   on every other status, so it may be passed uninitialised.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED, with the status that stopped
///                   the read. Untouched on every other status.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
/// @note READY and FAILED are both terminal: the answer is removed as it is reported, so asking a
///       second time reports UNKNOWN. UNKNOWN alone means "request it (again)".
DelugeChannelStatus deluge_file_bytes_collect_read(const char* path, uint8_t* out_buf, uint32_t buf_cap,
                                                   uint32_t* out_len, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `len` bytes to be written over `path`, and return immediately. [task]
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
///         REFUSED -- a request is already queued, an operation is in flight, or an uncollected rename, unlink or
///         write-from answer is waiting for its owner (see the module doc), or the channel was mid-update at the
///         instant of the call; or the path is NULL or over long, or `len` exceeds
///         @ref DELUGE_FILE_BYTES_MAX_BYTES. **A refused write did not happen and
///         nothing is queued**, so the caller must keep its intent and retry; unlike a read, a lost
///         request here is a lost settings change rather than a question worth re-asking.
bool deluge_file_bytes_request_write(const char* path, const uint8_t* bytes, uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the write to `path`, if it has finished. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED. READY means the bytes
///         reached the card and the caller's buffer is free again.
DelugeChannelStatus deluge_file_bytes_collect_write(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `from` to be renamed to `to`, and return immediately. [task]
///
/// For the settings family's legacy-path migration, which moves a file left at the card root into
/// `SETTINGS/`. Keyed on `to`, because that is the name the caller goes on to use.
///
/// @param from NUL-terminated source path. Copied out before this returns.
/// @param to   NUL-terminated destination path. Copied out before this returns.
/// @return true if accepted; the caller must then collect it to a terminal state (see "Collect what you ask for").
///         false if REFUSED -- a request is already queued, an operation is in flight, or an uncollected rename, unlink
///         or write-from answer is waiting for its owner (see the module doc), or the channel was mid-update at the
///         instant of the call; or either path is NULL or over long. Refused like a write
///         rather than dropped like a read: a migration that silently did not happen leaves the file where the next
///         boot will migrate it again.
bool deluge_file_bytes_request_rename(const char* from, const char* to) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the rename to `to`, if it has finished. [task]
///
/// @param to         NUL-terminated destination path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
/// @note A rename whose source does not exist reports FAILED with `DELUGE_ERR_NOT_FOUND`. For the
///       migration callers that is the ordinary case -- there is no legacy file -- and not an error
///       to show anyone.
DelugeChannelStatus deluge_file_bytes_collect_rename(const char* to, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path` to be deleted, and return immediately. [task]
///
/// @param path NUL-terminated path. Copied out before this returns.
/// @return true if accepted; the caller must then collect it to a terminal state (see "Collect what you ask for").
///         false if REFUSED -- a request is already queued, an operation is in flight, or an uncollected rename, unlink
///         or write-from answer is waiting for its owner (see the module doc), or the channel was mid-update at the
///         instant of the call; or the path is NULL or over long. Refused like a write rather
///         than dropped like a read, because a caller that believes a file is gone when it is not will act on that
///         belief.
bool deluge_file_bytes_request_unlink(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the deletion of `path`, if it has finished. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
/// @note Deleting a path that was already gone reports FAILED with `DELUGE_ERR_NOT_FOUND`. For the
///       startup fail-safe's cleanup that is success by another name -- the file is not there, which
///       is what was wanted -- so that caller must not treat it as an error.
DelugeChannelStatus deluge_file_bytes_collect_unlink(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask whether `path` exists, and return immediately. [task]
///
/// **Not a read.** Answering existence by requesting @ref deluge_file_bytes_request_read and reading
/// `NOT_FOUND` as absence works only for files below @ref DELUGE_FILE_BYTES_MAX_BYTES: a larger file
/// that IS there reports FAILED with `DELUGE_ERR_NO_MEMORY`, and a caller collapsing the two would
/// read "present" as "absent" and may then overwrite it. Song files are far above that bound, which
/// is why this operation exists rather than being expressed in terms of the read.
///
/// Silently dropped exactly as @ref deluge_file_bytes_request_read is: when the path is NULL or over long, or while
/// the channel is busy (a request queued, an operation in flight, an uncollected rename, unlink or write-from answer
/// waiting for its owner, or the channel mid-update). The next collect reports PENDING if this path's check is the
/// one in flight, and UNKNOWN, whose documented meaning is "ask again", otherwise.
///
/// @param path NUL-terminated path. Copied out before this returns.
void deluge_file_bytes_request_exists(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the existence answer for `path`, if there is one. [task]
///
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_present Written **only** on @ref DELUGE_CHANNEL_READY: true if the path is there,
///                    false if it is known not to be. Untouched on every other status, so it may be
///                    passed uninitialised.
/// @param out_status  Written **only** on @ref DELUGE_CHANNEL_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
/// @note THREE-STATE, and the third state is the point. READY with `*out_present == false` means
///       "known absent"; FAILED means "could not establish", which is NOT absence. A caller that
///       collapses FAILED into "absent" will write over files that are still there -- see
///       `existence_policy.h`'s `StartupPresence`, which is this distinction given a name.
DelugeChannelStatus deluge_file_bytes_collect_exists(const char* path, bool* out_present,
                                                     DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask whether `path` exists as a DIRECTORY, and return immediately. [task]
///
/// @ref deluge_file_bytes_request_exists opens its path as a file, which a directory is not, so it answers FAILED for
/// every directory. This one opens the path as a directory and closes it again before answering, so no directory
/// handle is held past the answer. Otherwise it behaves as that request, and is silently dropped under the same
/// conditions: a NULL or over-long path, or a busy channel (a request queued, an operation in flight, an uncollected
/// rename, unlink or write-from answer waiting for its owner, or the channel mid-update).
///
/// @param path NUL-terminated path. Copied out before this returns.
void deluge_file_bytes_request_dir_exists(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the directory-existence answer for `path`, if there is one. [task]
///
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_present Written **only** on @ref DELUGE_CHANNEL_READY: true if the directory is there, false if it is
///                    known not to be. Untouched on every other status.
/// @param out_status  Written **only** on @ref DELUGE_CHANNEL_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
/// @note Three-state, exactly as @ref deluge_file_bytes_collect_exists: FAILED is "could not establish", which is NOT
///       absence.
DelugeChannelStatus deluge_file_bytes_collect_dir_exists(const char* path, bool* out_present,
                                                         DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path` and every missing directory above it to be created, and return immediately. [task]
///
/// Whole-path, not one segment. A caller that walked segments itself would need a phase machine per
/// caller to do it over this channel, and both callers that need this already have a segment loop
/// that this deletes. It is also the shape the channel already has: @ref
/// deluge_file_bytes_request_write creates its destination's parent directory, so segment arithmetic
/// is below this boundary already.
///
/// @param path NUL-terminated path of the deepest directory wanted. Copied out before this returns.
/// @return true if accepted. false if REFUSED -- a request is already queued, an operation is in flight, or an
///         uncollected rename, unlink or write-from answer is waiting for its owner (see the module doc), or the
///         channel was mid-update at the instant of the call; or the path is NULL or over long.
/// @note An already-existing path is SUCCESS, not `DELUGE_ERR_EXISTS`. Callers use this to mean "make
///       sure this is there", and the commonest outcome is that it already is.
bool deluge_file_bytes_request_mkdir_all(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the directory creation for `path`, if it has finished. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED, with the first failure that
///                   was not "already exists". Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
DelugeChannelStatus deluge_file_bytes_collect_mkdir_all(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path`'s modification time to be set, and return immediately. [task]
///
/// Its own operation rather than an optional parameter on @ref deluge_file_bytes_request_mkdir_all,
/// because only two of its four callers follow a directory creation: the other two stamp an arbitrary
/// path on request and stamp a file after writing it.
///
/// @param path      NUL-terminated path. Copied out before this returns.
/// @param timestamp Passed BY VALUE. `DelugeTimestamp` is a fixed-width POD (`types.h`), so it
///                  crosses this boundary without a pointer and without a lifetime rule.
/// @return true if accepted. false if REFUSED -- a request is already queued, an operation is in flight, or an
///         uncollected rename, unlink or write-from answer is waiting for its owner (see the module doc), or the
///         channel was mid-update at the instant of the call; or the path is NULL or over long.
bool deluge_file_bytes_request_set_time(const char* path, DelugeTimestamp timestamp) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the timestamp write for `path`, if it has finished. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED. Untouched otherwise.
/// @return One of @ref DELUGE_CHANNEL_PENDING, @ref DELUGE_CHANNEL_READY,
///         @ref DELUGE_CHANNEL_UNKNOWN, @ref DELUGE_CHANNEL_FAILED.
DelugeChannelStatus deluge_file_bytes_collect_set_time(const char* path, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for up to @ref DELUGE_FILE_BYTES_BLOCK bytes to be read from `path` at `offset`. [task]
///
/// Stateless: the path and offset are the whole request, and no handle is opened, held or returned.
/// `smsysex`'s `readBlock` already seeks to an address carried on the wire before every read, so this
/// matches its consumer exactly.
///
/// @param path   NUL-terminated path. Copied out before this returns.
/// @param offset Byte offset to read from. Past end-of-file yields zero bytes, not an error.
/// @param len    How many bytes to read, at most @ref DELUGE_FILE_BYTES_BLOCK.
/// @return true if accepted. false if REFUSED -- a request is already queued, an operation is in flight, or an
///         uncollected rename, unlink or write-from answer is waiting for its owner (see the module doc), or the
///         channel was mid-update at the instant of the call; or the path is NULL or over long, or `len` exceeds the
///         block bound.
bool deluge_file_bytes_request_read_at(const char* path, uint32_t offset, uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the bytes read for `path`, if they are ready. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_buf    Written **only** on @ref DELUGE_CHANNEL_READY, with `*out_len` bytes.
/// @param buf_cap    Capacity of `out_buf`. A read longer than this reports
///                   @ref DELUGE_CHANNEL_FAILED with `DELUGE_ERR_NO_MEMORY`.
/// @param out_len    Written **only** on READY. **A short count is not an error**: it means
///                   end-of-file was reached, which is how a caller detects it.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED.
DelugeChannelStatus deluge_file_bytes_collect_read_at(const char* path, uint8_t* out_buf, uint32_t buf_cap,
                                                      uint32_t* out_len, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `len` bytes to be written into `path` at `offset`. [task]
///
/// @warning **This does NOT truncate, and must not.** `efatfs::portable::create_context` truncates
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
/// @return true if accepted. false if REFUSED -- a request is already queued, an operation is in flight, or an
///         uncollected rename, unlink or write-from answer is waiting for its owner (see the module doc), or the
///         channel was mid-update at the instant of the call; or the path or `bytes` is NULL, the path is over long, or
///         `len` exceeds the block bound.
bool deluge_file_bytes_request_write_at(const char* path, uint32_t offset, const uint8_t* bytes,
                                        uint32_t len) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the write to `path`, if it has finished. [task]
///
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_written Written **only** on READY, with the byte count actually written. A short write
///                    is reported honestly rather than as success; the caller decides what it means.
/// @param out_status  Written **only** on FAILED.
DelugeChannelStatus deluge_file_bytes_collect_write_at(const char* path, uint32_t* out_written,
                                                       DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `path`'s size. [task]
///
/// Silently dropped exactly as @ref deluge_file_bytes_request_read is: when the path is NULL or over long, or while
/// the channel is busy (a request queued, an operation in flight, an uncollected rename, unlink or write-from answer
/// waiting for its owner, or the channel mid-update). The next collect reports PENDING if this path's stat is the one
/// in flight, and UNKNOWN, whose documented meaning is "ask again", otherwise.
/// @param path NUL-terminated path. Copied out before this returns.
void deluge_file_bytes_request_stat(const char* path) DELUGE_NOEXCEPT;

/// @brief Collect `path`'s size, if it is known. [task]
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_size   Written **only** on @ref DELUGE_CHANNEL_READY.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED, with `DELUGE_ERR_NOT_FOUND`
///                   for a path that is not there.
DelugeChannelStatus deluge_file_bytes_collect_stat(const char* path, uint32_t* out_size,
                                                   DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for `from` to be copied to `to`, whole. [task]
///
/// The entire copy, not a block of it: the read/write loop lives on the Rust side so its caller needs
/// neither a loop nor two open files. `to`'s parent directory is created if absent, and `to` is
/// truncated before the copy -- a copy onto a longer existing file must not leave its tail behind. The channel's
/// progress mark (@ref deluge_file_bytes_progress) advances with every @ref DELUGE_FILE_BYTES_BLOCK it moves.
/// @return true if accepted. false if REFUSED -- a request is already queued, an operation is in flight, or an
///         uncollected rename, unlink or write-from answer is waiting for its owner (see the module doc), or the
///         channel was mid-update at the instant of the call; or either path is NULL or over long.
bool deluge_file_bytes_request_copy(const char* from, const char* to) DELUGE_NOEXCEPT;

/// @brief Collect the outcome of the copy to `to`, if it has finished. [task]
///
/// Keyed on the DESTINATION, because that is the name the caller goes on to use.
/// @param to         NUL-terminated destination, matched exactly against the one requested.
/// @param out_status Written **only** on @ref DELUGE_CHANNEL_FAILED.
DelugeChannelStatus deluge_file_bytes_collect_copy(const char* to, DelugeStatus* out_status) DELUGE_NOEXCEPT;

/// @brief Ask for the whole of `path` to be read into `dst`.
///
/// `dst` is the caller's, kept alive and untouched until the collect is terminal (see deluge_file_bytes_holds).
/// [task]
///
/// Not bounded by @ref DELUGE_FILE_BYTES_MAX_BYTES: the bytes are not copied, so a song of a megabyte reads straight
/// into its staging buffer. A file larger than `cap` is refused, never truncated.
/// @param path NUL-terminated path. Copied out before this returns.
/// @param dst  The caller's buffer, `cap` bytes.
/// @param cap  Its capacity.
/// @return true if accepted. false if REFUSED -- a request is already queued, an operation is in flight, or an
///         uncollected rename, unlink or write-from answer is waiting for its owner (see the module doc), or the
///         channel was mid-update at the instant of the call; or the path or `dst` is NULL, the path is over long, or
///         `cap` is 0. The caller must retry a refusal.
bool deluge_file_bytes_request_read_into(const char* path, void* dst, uint32_t cap) DELUGE_NOEXCEPT;

/// @brief Collect the read into the caller's buffer.
///
/// READY: `*out_len` bytes are in `dst`. FAILED: `*out_error` says why; DELUGE_ERR_NO_MEMORY means the file did not
/// fit, and `*out_needed` is its size. A file that could not be opened reports the open's own status --
/// DELUGE_ERR_NOT_FOUND for a missing file -- and an empty or short read DELUGE_ERR_IO. Any out-pointer may be null.
/// [task]
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param out_len    Written **only** on @ref DELUGE_CHANNEL_READY.
/// @param out_error  Written **only** on @ref DELUGE_CHANNEL_FAILED.
/// @param out_needed Written **only** on @ref DELUGE_CHANNEL_FAILED: the file's size when it did not fit, else 0.
DelugeChannelStatus deluge_file_bytes_collect_read_into(const char* path, uint32_t* out_len, DelugeStatus* out_error,
                                                        uint32_t* out_needed) DELUGE_NOEXCEPT;

/// @brief Ask for the caller's bytes to be written as the whole of `path`.
///
/// With `may_overwrite` false, the file must not exist; that check and the create are one atomic open. `src` is the
/// caller's until the collect is terminal. [task]
///
/// Not bounded by @ref DELUGE_FILE_BYTES_MAX_BYTES: the bytes are not copied. The create makes every missing
/// directory above `path`.
/// @param path          NUL-terminated path. Copied out before this returns.
/// @param src           The caller's bytes.
/// @param len           Byte count.
/// @param may_overwrite true to replace an existing file, false to refuse one.
/// @return true if accepted; the caller must then collect it to a terminal state (see "Collect what you ask for").
///         false if REFUSED -- a request is already queued, an operation is in flight, or an uncollected rename, unlink
///         or write-from answer is waiting for its owner (see the module doc), or the channel was mid-update at the
///         instant of the call; or the path or `src` is NULL, the path is over long, or `len` is
///         0. The caller must retry a refusal.
bool deluge_file_bytes_request_write_from(const char* path, const void* src, uint32_t len,
                                          bool may_overwrite) DELUGE_NOEXCEPT;

/// @brief Collect the write from the caller's buffer.
///
/// READY: all `*out_written` bytes are on the card. FAILED: `*out_error` says why; DELUGE_ERR_EXISTS means the name
/// was taken and the existing file is untouched. Any other failed open reports the open's own status (a name the
/// card refuses, a missing card), and a short write DELUGE_ERR_IO. [task]
///
/// "The name is taken" asks the user a question; "the card failed" tells them something is wrong, so the two stay
/// apart.
/// @param path        NUL-terminated path, matched exactly against the one requested.
/// @param out_written Written **only** on @ref DELUGE_CHANNEL_READY. May be null.
/// @param out_error   Written **only** on @ref DELUGE_CHANNEL_FAILED. May be null.
DelugeChannelStatus deluge_file_bytes_collect_write_from(const char* path, uint32_t* out_written,
                                                         DelugeStatus* out_error) DELUGE_NOEXCEPT;

/// @brief Whether a queued or running byte request reads or writes through `buffer`.
///
/// True when it cannot tell. [task]
///
/// The read-into and write-from operations cannot be cancelled, so a caller that gives up on one must not free or
/// reuse its buffer while this is true.
/// @param buffer The buffer handed to a request, as passed (its start address).
bool deluge_file_bytes_holds(const void* buffer) DELUGE_NOEXCEPT;

/// @brief How much work the channel has finished, as a mark that moves whenever it finishes some. [task]
///
/// Advances once per operation finished (answered, or dropped unrun because its card has gone) and once per
/// @ref DELUGE_FILE_BYTES_BLOCK a copy moves. A caller that is refused, or kept PENDING, reads it each tick and counts
/// only the ticks on which it did not move against its stall bound (`deluge::io::StallBound::tick(uint32_t)`): a
/// channel busy with someone else's long operation keeps moving, and a wedged one stands still.
/// @return The mark. It wraps, so compare two readings for equality only.
uint32_t deluge_file_bytes_progress(void) DELUGE_NOEXCEPT;

#ifdef DELUGE_HOST
/// @brief Host harness only: make every byte-channel operation wait @p ms milliseconds before it touches the card.
/// [task]
///
/// Lets a gate hold an operation in flight while a consumer does something else. 0 (the default) means no wait.
/// Declared only for host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per operation.
void deluge_file_bytes_set_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;

/// @brief Host harness only: make each @ref DELUGE_FILE_BYTES_BLOCK a copy moves wait @p ms milliseconds first. [task]
///
/// Holds the channel with a long operation that keeps making progress, as a large copy on a slow card does, where
/// @ref deluge_file_bytes_set_latency_ms holds one that makes none. 0 (the default) means no wait. Declared only for
/// host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per chunk.
void deluge_file_bytes_set_chunk_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;

/// @brief Host harness only: make every @ref deluge_file_bytes_request_write_from operation wait @p ms milliseconds
/// before it touches the card, on top of @ref deluge_file_bytes_set_latency_ms. [task]
///
/// Holds a save's document write without holding the existence checks, read-backs and renames around it, which a
/// save gives up on past its stall bound. 0 (the default) means no wait. Declared only for host builds, and defined
/// only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per document write.
void deluge_file_bytes_set_write_from_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;
#endif

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_FILE_BYTES_H
