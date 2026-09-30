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
/// @brief An OPEN directory, over the C ABI, without holding the caller.
///
/// Request/collect, the same non-blocking shape the channels in @ref audio_load.h and
/// @ref file_prefix.h already use: ask for a directory, return to your own loop, and collect its
/// handle on a later tick. It exists because opening a directory is the last blocking call in the
/// browser's listing path -- `deluge_efatfs_dir_open` bridges to the real open through a spin, and
/// the caller is on the interaction tier, which may never wait on a lower one.
///
/// **It opens; it does not enumerate.** Walking the opened directory costs no card I/O at all: the
/// open takes a snapshot and `deluge_efatfs_dir_read` walks it in memory. So the caller enumerates
/// inline, to completion, in the tick that collects. Only the open crosses this boundary, because
/// only the open touches the card.
///
/// **Single-flight by construction.** The backing is one slot, not a queue: at most one open is
/// ever outstanding, and a new @ref deluge_file_directory_request REPLACES one that has not been
/// picked up yet. That is the right shape for a browser, which shows one folder at a time.
///
/// **A collected handle is the caller's, and closing it is not optional.** There are only eight
/// directory handles. A caller that collects READY and then walks away without closing has burned
/// one of them permanently; eight such and directory browsing stops working. On the C++ side
/// `deluge::io::Directory::adopt` exists precisely so the handle re-enters RAII the moment it
/// arrives.
///
/// **Fixed-width, never a native enum.** Every value crossing this boundary is a fixed-width
/// integer, for the reason @ref audio_load.h gives: `arm-none-eabi` defaults to short enums, so a
/// native enum layout must never be relied on across it. `DelugeStatus` is itself declared with a
/// fixed underlying type (`int8_t`), which is why it may appear here at all.
#ifndef LIBDELUGE_FILE_DIRECTORY_H
#define LIBDELUGE_FILE_DIRECTORY_H

#include "libdeluge/types.h"

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"
#include "channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief The longest path this channel can key on, INCLUDING the NUL — so the text it holds is one
///        byte shorter.
///
/// @note A longer path is REFUSED, never truncated — a truncated path names a *different* directory,
///       which may well exist. @ref deluge_file_directory_request drops it silently, so a caller that
///       does not check this itself gets UNKNOWN forever with no error to show.
#define DELUGE_FILE_DIRECTORY_MAX_PATH DELUGE_PATH_MAX

/// @brief @ref deluge_file_directory_collect: requested, or being opened right now.
///
/// Ask again on a later tick. Do NOT re-request: the open is already under way, and re-requesting
/// would replace a request that is about to be served.
#define DELUGE_FILE_DIRECTORY_PENDING DELUGE_CHANNEL_PENDING

/// @brief @ref deluge_file_directory_collect: a handle was written out and removed from the slot.
///
/// `out_handle` holds an OPEN directory that is now the caller's to close. Terminal: the answer has
/// been consumed, so a second collect for the same path reports UNKNOWN.
#define DELUGE_FILE_DIRECTORY_READY DELUGE_CHANNEL_READY

/// @brief @ref deluge_file_directory_collect: nothing is known about this path.
///
/// Request it (again). This is also what a caller sees before its first request, and what it sees
/// after a card swap discarded an answer read from the card that has gone.
#define DELUGE_FILE_DIRECTORY_UNKNOWN DELUGE_CHANNEL_UNKNOWN

/// @brief @ref deluge_file_directory_collect: the open did not happen. `out_status` says why.
///
/// TERMINAL -- do NOT re-request. A folder that cannot be opened would otherwise be re-requested and
/// re-opened on every tick, forever, for as long as the browser sits on it. Unlike
/// @ref file_prefix.h's failure this one carries a status, because the browser's failure path shows
/// the user which failure it was, and "no such folder" must not become "the card is unreadable".
#define DELUGE_FILE_DIRECTORY_FAILED DELUGE_CHANNEL_FAILED

/// @brief Ask for `dir_path` to be opened, and return immediately.
///
/// Replaces any request not yet picked up. Silently drops an unusable request (NULL, non-UTF-8, over
/// long, or a contended slot): there is no failure a caller could act on that re-requesting does not
/// already cover, and the collect it runs next tick reports @ref DELUGE_FILE_DIRECTORY_UNKNOWN,
/// whose documented meaning is exactly that.
/// @param dir_path NUL-terminated path of the directory to open. Copied out before this returns.
/// @param create_if_missing Create `dir_path` before opening it. For the browser's missing-folder
///        fallback, which creates its default directory and lists that. The creation happens on the
///        storage tier with the open, because a card WRITE is no more allowed on the interaction
///        tier than a read is — and doing it here rather than in the caller is what keeps it there.
///        Its own failure is not reported: the OPEN is the verdict, and the commonest outcome is
///        "already exists", which is not a failure for the caller's purpose.
void deluge_file_directory_request(const char* dir_path, bool create_if_missing) DELUGE_NOEXCEPT;

/// @brief Collect the directory opened for `dir_path`, if one is ready.
///
/// @param dir_path   NUL-terminated path, matched exactly against the one requested.
/// @param out_handle Written **only** on @ref DELUGE_FILE_DIRECTORY_READY, with an open directory
///                   handle the caller then owns and must close. Untouched on every other status,
///                   so it may be passed uninitialised.
/// @param out_status Written **only** on @ref DELUGE_FILE_DIRECTORY_FAILED, with the status that
///                   stopped the open. Untouched on every other status.
/// @return One of @ref DELUGE_FILE_DIRECTORY_PENDING, @ref DELUGE_FILE_DIRECTORY_READY,
///         @ref DELUGE_FILE_DIRECTORY_UNKNOWN, @ref DELUGE_FILE_DIRECTORY_FAILED.
/// @note READY and FAILED are both terminal: the answer is removed as it is reported, so asking a
///       second time reports UNKNOWN. UNKNOWN alone means "request it (again)".
DelugeChannelStatus deluge_file_directory_collect(const char* dir_path, DelugeHandle* out_handle,
                                                  DelugeStatus* out_status) DELUGE_NOEXCEPT;

#ifdef DELUGE_HOST
/// @brief Host harness only: make every directory open wait @p ms milliseconds before it touches the card.
///
/// Lets a gate hold an open in flight while a consumer keeps ticking. 0 (the default) means no wait.
/// Declared only for host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per open.
void deluge_file_directory_set_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;
#endif

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_FILE_DIRECTORY_H
