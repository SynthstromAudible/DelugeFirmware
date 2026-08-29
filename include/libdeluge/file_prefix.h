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
/// **What this boundary does NOT do.** It has no opinion about the bytes. It does not parse and does
/// not know a song from a sample. It reports no error CODE either -- there is no `Error` on this
/// boundary -- but it does report the one bit a caller cannot reconstruct: whether the read happened
/// at all. A file that would not open, could not be sized, or failed its read comes back as
/// @ref DELUGE_FILE_PREFIX_FAILED, which is terminal exactly as READY is; see
/// @ref deluge_file_prefix_collect.
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
/// @ref deluge_file_prefix_request clamps a larger `max_bytes` to it rather than failing.
///
/// Sized from the channel's LARGEST consumer -- the DX7 cartridge's `min(filesize, 8192)` read --
/// not from any one caller's window. It is deliberately NOT equal to
/// `deluge::storage::kSongPreviewWindow` (4096): that is a request size, this is the slot every
/// request's result lands in. `file_prefix_abi.cpp` `static_assert`s the relation that survives
/// them being different, namely that a request can never exceed the slot.
///
/// @note A collect's `out_cap` must be at least this, not merely as large as the caller's own
///       `max_bytes` -- see @ref deluge_file_prefix_collect. Growing this therefore grows every
///       caller's buffer.
#define DELUGE_FILE_PREFIX_CAPACITY 8192u

/* The first three status values below are NUMERICALLY IDENTICAL to audio_load.h's
 * DELUGE_AUDIO_LOAD_PENDING/_READY/_UNKNOWN, and must stay that way. Both headers will plausibly be
 * included in one translation unit -- browsing a song wants its samples AND its artwork -- where
 * `deluge_file_prefix_collect(..) == DELUGE_AUDIO_LOAD_READY` compiles clean and nothing
 * cross-checks the two sets (the Rust BSP's bindgen allowlist covers `Deluge.*` types, not macros).
 * If the numbering diverged, that expression would silently mean PENDING: the caller would read an
 * `out_buf` this header promises is untouched -- uninitialised bytes decoded as artwork -- while a
 * real READY read as PENDING and polled forever. */

/// @ref deluge_file_prefix_collect: the read is requested or running. Ask again next tick; do NOT
/// re-request.
#define DELUGE_FILE_PREFIX_PENDING 0u
/// @ref deluge_file_prefix_collect: a prefix was written to `out_buf`/`out_len` and REMOVED from the
/// channel. A second collect for the same path reports @ref DELUGE_FILE_PREFIX_UNKNOWN.
#define DELUGE_FILE_PREFIX_READY 1u
/// @ref deluge_file_prefix_collect: the channel knows nothing about this path. Either it was never
/// requested, its prefix was already collected, its request was superseded by another path's, or the
/// card it was read from has since been ejected. The caller should request it (again).
#define DELUGE_FILE_PREFIX_UNKNOWN 2u
/// @ref deluge_file_prefix_collect: the read did NOT happen -- the file would not open, could not be
/// sized, or the read itself failed -- and the failure is REMOVED from the channel, exactly as a
/// prefix is. Terminal, like @ref DELUGE_FILE_PREFIX_READY: do NOT re-request, or an unreadable file
/// is re-opened on every tick for as long as the caller stays interested. Neither `out_buf` nor
/// `out_len` is written.
///
/// The channel does not say WHICH failure it was, and deliberately so: it has no error vocabulary,
/// and the synchronous file path it stands in for does not distinguish them either
/// (`StorageManager::openXMLFile` reports `Error::FILE_NOT_FOUND` for every failed open, busy and
/// unreadable included). What this value carries is the one distinction a caller cannot make for
/// itself -- "no bytes because the read failed" versus "no bytes because the file is empty" -- and
/// without it the two are the same READY-with-zero-length answer, so a caller that shows an error
/// for one and blank artwork for the other cannot tell them apart.
///
/// Numerically outside audio_load.h's set on purpose: that header has three values and this one now
/// has four, so a caller that cross-compares (see the note above) reads an unrecognised status
/// rather than a wrong one.
#define DELUGE_FILE_PREFIX_FAILED 3u

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
/// comes back as @ref DELUGE_FILE_PREFIX_FAILED, which is terminal in exactly the way READY is: the
/// entry is removed and the caller must not re-request. Reporting UNKNOWN instead would mean
/// "request it (again)", and a caller polling a file it cannot read would re-request and re-read it
/// on every tick for as long as it stayed interested.
///
/// It was previously reported as READY with an `out_len` of 0, which is terminal too but which a
/// caller cannot tell from a genuinely EMPTY file. That mattered: the synchronous backing this
/// channel stands in for shows the user an error for a song it cannot open, and a caller served
/// identical bytes for "unreadable" and "no artwork" has no way to keep doing so. A zero-length
/// READY now means one thing only -- the file is empty.
///
/// @param file_path The path passed to @ref deluge_file_prefix_request. Must not be NULL.
/// @param out_buf   Where the bytes go. Must not be NULL. Written only on READY.
/// @param out_cap   How many bytes @p out_buf can hold. **Must be at least
///                  @ref DELUGE_FILE_PREFIX_CAPACITY**, which is the largest prefix this channel
///                  can be holding. A debug build asserts this, so the caller bug is reported at
///                  its cause; a release build degrades TERMINALLY -- the prefix is consumed and
///                  reported as READY with an @p out_len of 0, i.e. blank artwork -- rather than
///                  serving truncated bytes.
///
///                  Deliberately NOT "left for a correctly-sized retry": that reported UNKNOWN,
///                  which tells the caller to request again, which re-reads the file FROM THE CARD
///                  and republishes a prefix the same small buffer cannot take. Once per tick, with
///                  real SD I/O each time, for as long as the caller stays interested.
/// @param out_len   Receives how many bytes were written to @p out_buf. Must not be NULL. Written
///                  only on READY, where it may legitimately be 0.
/// @return One of `DELUGE_FILE_PREFIX_READY` / `_PENDING` / `_UNKNOWN` / `_FAILED`.
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
