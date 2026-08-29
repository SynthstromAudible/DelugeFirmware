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
/// @brief The first N bytes of a file, over the C ABI, without holding the caller.
///
/// Request/collect, the same non-blocking shape @ref audio_load.h and `streaming_fill.h` already
/// use: ask for a prefix, return to your own loop, and collect it on a later tick. It exists for
/// callers that need to look at the START of a file from a tier that is not allowed to wait on the
/// card -- a browser drawing song artwork being the case it was built for, where the alternative is
/// a blocking read on the interaction tier.
///
/// **Single-flight by construction.** The backing is one slot, not a queue: at most one prefix is
/// ever outstanding, and a new @ref deluge_file_prefix_request REPLACES one that has not been picked
/// up yet. That is the right shape for a cursor moving through a folder -- the path the cursor left
/// behind is no longer wanted -- and it means a caller that interleaves requests for two paths will
/// see one of them answered and the other report @ref DELUGE_FILE_PREFIX_UNKNOWN. Callers that need
/// both must ask for them one at a time.
///
/// **What this boundary does NOT do.** It has no opinion about the bytes. It does not parse, does
/// not know a song from a sample, and reports no error code: a file that will not open, cannot be
/// sized, or fails its read comes back as an EMPTY prefix rather than as a failure. That is
/// deliberate -- see @ref deluge_file_prefix_collect -- and it is why there is no `Error` on this
/// boundary at all.
///
/// **Fixed-width, never a native enum.** Every value crossing this boundary is a fixed-width
/// integer, for the reason @ref audio_load.h gives: `arm-none-eabi` defaults to short enums, so a
/// native enum layout must never be relied on across it.
#ifndef LIBDELUGE_FILE_PREFIX_H
#define LIBDELUGE_FILE_PREFIX_H

#include <stdbool.h>
#include <stdint.h>

/* Not currently parsed as C -- same situation as audio_load.h, and the same forward-looking
 * hardening: `noexcept` is C++-only and invalid C syntax, so it is spelled through this macro
 * (#undef'd at the end of the file so it cannot leak into, or collide with a same-named macro in,
 * whatever includes this header next) rather than written bare. */
#ifdef __cplusplus
#define DELUGE_NOEXCEPT noexcept
extern "C" {
#else
#define DELUGE_NOEXCEPT
#endif

/// @brief The most bytes one prefix can be, and therefore the most a request can usefully ask for.
///
/// The channel holds the finished prefix in a fixed slot of exactly this size, so
/// @ref deluge_file_prefix_request clamps a larger `max_bytes` to it rather than failing. Sized from
/// `deluge::storage::kSongPreviewWindow`, which `file_prefix_abi.cpp` `static_assert`s this against
/// so the two cannot drift.
#define DELUGE_FILE_PREFIX_CAPACITY 4096u

/// @ref deluge_file_prefix_collect: a prefix was written to `out_buf`/`out_len` and REMOVED from the
/// channel. A second collect for the same path reports @ref DELUGE_FILE_PREFIX_UNKNOWN.
#define DELUGE_FILE_PREFIX_READY 0u
/// @ref deluge_file_prefix_collect: the read is requested or running. Ask again next tick; do NOT
/// re-request.
#define DELUGE_FILE_PREFIX_PENDING 1u
/// @ref deluge_file_prefix_collect: the channel knows nothing about this path. Either it was never
/// requested, its prefix was already collected, its request was superseded by another path's, or the
/// card it was read from has since been ejected. The caller should request it (again).
#define DELUGE_FILE_PREFIX_UNKNOWN 2u

/// @brief Ask for the first @p max_bytes of @p file_path. Returns immediately; never waits.
///
/// Pair with @ref deluge_file_prefix_collect: request, return to your own loop, and ask for the
/// bytes on a later tick.
///
/// Requesting the same path again while it is still pending is harmless and costs at most one
/// redundant read. Requesting a DIFFERENT path discards the outstanding one -- see this header's
/// single-flight note.
///
/// @param file_path The path to read, and the key @ref deluge_file_prefix_collect looks the result
///                  up by. Must not be NULL. Copied out before this returns.
/// @param max_bytes How many bytes from the start of the file to read. Clamped to
///                  @ref DELUGE_FILE_PREFIX_CAPACITY. A shorter file yields a shorter prefix.
void deluge_file_prefix_request(const char* file_path, uint32_t max_bytes) DELUGE_NOEXCEPT;

/// @brief Collect the prefix read for @p file_path, if one is ready. Never waits.
///
/// On @ref DELUGE_FILE_PREFIX_READY the bytes are written to @p out_buf, their count to @p out_len,
/// and the prefix is REMOVED from the channel, so the caller now owns the only copy. **Neither
/// output is written on any other status** -- a caller may pass an uninitialised buffer and read it
/// back only on READY.
///
/// A read that failed -- the file would not open, could not be sized, or the read itself failed --
/// comes back as READY with an `out_len` of 0, indistinguishable from a genuinely empty file. That
/// is the deliberate choice, not an oversight: reporting UNKNOWN instead would mean "request it
/// (again)", and a caller polling a file it cannot read would re-request and re-read it on every
/// tick for as long as it stayed interested. An empty prefix is a terminal answer, and for the
/// artwork case it decodes to the correct picture -- blank.
///
/// @param file_path The path passed to @ref deluge_file_prefix_request. Must not be NULL.
/// @param out_buf   Where the bytes go. Must not be NULL. Written only on READY.
/// @param out_cap   How many bytes @p out_buf can hold. **Must be at least the `max_bytes` that was
///                  requested** (or @ref DELUGE_FILE_PREFIX_CAPACITY, which always suffices): a
///                  buffer too small for the prefix that is waiting is a caller bug, and it is
///                  reported as UNKNOWN with the prefix left where it is rather than served
///                  truncated. Re-collecting with a big enough buffer still yields it.
/// @param out_len   Receives how many bytes were written to @p out_buf. Must not be NULL. Written
///                  only on READY, where it may legitimately be 0.
/// @return One of `DELUGE_FILE_PREFIX_READY` / `_PENDING` / `_UNKNOWN`.
uint8_t deluge_file_prefix_collect(const char* file_path, uint8_t* out_buf, uint32_t out_cap,
                                   uint32_t* out_len) DELUGE_NOEXCEPT;

/* Discarding a prefix read from a card that has been ejected is deluge_card_flush (card.h), not a
 * call of this header's own: an ejection invalidates every loader at once, and they share one card
 * generation to say so. Deliberately not a doc comment -- it documents an absence, and would
 * otherwise attach itself to the declaration below. */

/// @brief Whether the Rust async prefix channel backs this header on this BSP/config.
/// @return true if a request will actually be read; false if every caller must read the file
///         itself, synchronously. When this is false, @ref deluge_file_prefix_request does nothing
///         and @ref deluge_file_prefix_collect always reports UNKNOWN.
bool deluge_file_prefix_async_active(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#undef DELUGE_NOEXCEPT
#endif // LIBDELUGE_FILE_PREFIX_H
