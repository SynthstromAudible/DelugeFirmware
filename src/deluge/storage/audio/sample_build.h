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
/// @brief The card-sample build: the I/O-free prologue and epilogue around an async header parse.
///
/// `AudioFileManager::buildAudioFileFromCard`'s SAMPLE path splits into three parts: an I/O-free
/// prologue (allocate + adopt a `Sample`), the I/O (open the alternate dir, open the read stream,
/// open the file, parse its header), and an I/O-free epilogue (apply the parsed geometry, publish).
/// The async loader (libdeluge/audio_load.h) owns the file-open-and-parse I/O; these functions are
/// the halves around it, plus the candidate policy that feeds its request and the read-stream bind a
/// published build must have. All of them run on the requesting tier; none is part of the C ABI.
///
/// **Call order.** The candidate policy comes FIRST and on a different tier from the parse, because
/// it is neither I/O-free nor allowed on the loader's executor:
///
///   `candidates` (and, only where a load has already missed, `resolveAlternateDir` before it)
///   -> `deluge_audio_load_request` -> the caller returns to its own loop ->
///   `deluge_audio_load_collect`, on a later tick, until it reports READY -> `begin` ->
///   `bindReadStream` on the path the result names -> `applyGeometry` -> `finish` (or `abandon` at
///   any point after `begin`, including when `bindReadStream` or `applyGeometry` itself fails).
///
/// `begin` is deliberately LAST-minute rather than first: it adopts and reasons a `Sample`, so
/// opening a build before the I/O would mean owning one across every await. Nothing is adopted until
/// the bytes are already in hand.
///
/// **Ownership.** `begin` allocates and adopts a `Sample` and gives it a protect-during-setup reason
/// BEFORE any I/O runs. From the moment it returns a non-null build, exactly one of `finish` or
/// `abandon` MUST be called on it -- on every path: a clean parse, a parse error, a file-open
/// failure, or the card being pulled mid-load. A build that is never finished or abandoned sits in
/// `AudioFileManager::audioFiles` adopted, reasoned (so it can never be evicted), and still at zero
/// geometry, which makes every fill-context consumer that later looks it up silently resolve nothing
/// for that sample.
///
/// Separately, `bindReadStream` MUST be called on every build that reaches `finish` -- see its own
/// doc comment for what a published build without one actually does (nothing catches it; it just
/// plays silence).
///
/// Every function is `noexcept`: each catches whatever its body throws (the allocators throw
/// `deluge::exception::BAD_ALLOC`) and degrades to the failure return its doc comment names.
#pragma once

#include "definitions_cxx.hpp" // Error

#include "libdeluge/audio_load.h" // DelugeAudioLoadGeometry, DELUGE_PATH_MAX

#include <cstdint>
#include <string_view>

class AudioFile;

namespace deluge::storage {
struct AudioLoadContext;
} // namespace deluge::storage

namespace deluge::storage::sample_build {

/// @brief A card-sample build in flight: the `Sample` `beginSampleFromCard` allocated plus the
///        bookkeeping `finish`/`abandon` need to close it out. Opaque; owned exactly as this file's
///        ownership note says.
struct SampleBuild;

/// @brief The parsed header geometry `applyGeometry` writes onto a build. The loader's own result
///        type (`DelugeAudioLoadResult::geometry`), so it stays declared in libdeluge/audio_load.h.
using Geometry = DelugeAudioLoadGeometry;

/// @brief Allocate and construct a `Sample` from an already-resolved size. No I/O.
///
/// Adopts the object and gives it a protect-during-setup reason before returning, so from here the
/// caller owns its fate unconditionally -- see this file's ownership note. Validates @p sizeBytes
/// itself (`FILE_CORRUPTED` for 0, `FILE_TOO_BIG` over the 1 GB ceiling); the caller does not need to
/// re-check either.
///
/// @param filePath      The file's nominal/display path.
/// @param alternatePath The alternate-load-dir path segment the bytes actually live at, or empty if
///                      resolved at @p filePath directly.
/// @param sizeBytes     The resolved on-card file's size, in bytes.
/// @param error         Written with the outcome: `Error::NONE` on success, the failure otherwise. May
///                      be null if the caller does not need the code.
/// @return The new build, or null on failure (nothing adopted, no reason held).
[[nodiscard]] SampleBuild* begin(std::string_view filePath, std::string_view alternatePath, uint64_t sizeBytes,
                                 Error* error) noexcept;

/// @brief Resolve whether the alternate load dir exists, before proposing candidates.
///
/// I/O: opens the alternate directory. A song/preset load starts with the alternate-dir status
/// unresolved (`MIGHT_EXIST`), in which state `candidates` proposes only the regular path --
/// promoting that status to `DOES_EXIST`/`NOT_FOUND` is what unlocks the alternate-dir candidates,
/// and it is I/O, so it cannot live in the (I/O-free) candidates call itself.
///
/// **Call this AFTER a load has missed, never before its first `candidates`.** The context's status
/// is per-LOAD shared state: promoting it up front makes every LATER sample of the same load try the
/// alternate directory first, including the ones taking the synchronous path, which still promotes
/// only after a real miss. So the load's effective try order would flip out from under callers this
/// function never runs for.
///
/// The synchronous path (`AudioFileManager::resolveFileSize`) puts the probe after every proposed
/// candidate has actually missed, and the async collector matches it: a request carries whatever the
/// current status proposes (under `MIGHT_EXIST`, the regular path alone -- exactly the one candidate
/// `resolveFileSize` tries first from that same state), and a "no candidate opened" verdict
/// (`Error::FILE_UNREADABLE`) is what resolves the directory and asks again. See
/// `AudioFileManager::requestAsyncSampleLoad` and `answerFailedLoadResult`.
///
/// A no-op, doing no I/O, once the status is already resolved (from an earlier call in the same
/// load, or because nothing is loading) -- safe to call speculatively.
///
/// @param context The load whose alternate directory to probe; its status is updated. Null: no load,
///                so nothing to probe.
/// @param error   Written unconditionally (if non-null): `Error::NONE` whenever this returns true
///                (already resolved, or newly resolved as present); `Error::FILE_UNREADABLE` if the
///                probe finds the dir genuinely absent, or `Error::SD_CARD` if presence couldn't be
///                determined, when it returns false.
/// @return true once the alternate dir is known to exist (already, or newly resolved); false
///         otherwise.
bool resolveAlternateDir(AudioLoadContext* context, Error* error) noexcept;

/// @brief Write the candidate paths for @p filePath, in try order, into @p out. No I/O -- alternate-
///        load-dir policy only, including the SYNTH/KIT naming rules and the bare-filename fallback.
///
/// Call `resolveAlternateDir` first (see its doc comment) or this proposes only the regular path even
/// when an alternate dir exists.
///
/// @param context       The load whose policy applies. Null: no load, so the regular path alone.
/// @param filePath      The nominal path a load was asked for.
/// @param out           Buffer of at least `maxCandidates * DELUGE_PATH_MAX` bytes; each slot holds one
///                      NUL-terminated path, in try order -- the layout `deluge_audio_load_request` reads.
/// @param maxCandidates Capacity of @p out, in slots.
/// @return How many candidates were proposed in total. This can exceed @p maxCandidates, in which case
///         only the first @p maxCandidates were written to @p out -- the same snprintf-style
///         return-vs-capacity contract as `AudioFileManager::proposeLoadCandidates`.
///         `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED` if @p out is not trustworthy at all (a candidate
///         that would have been written did not fit in `DELUGE_PATH_MAX` bytes, or an internal
///         allocation failed) -- the caller must not read @p out in that case.
[[nodiscard]] uint32_t candidates(const AudioLoadContext* context, std::string_view filePath, char* out,
                                  uint32_t maxCandidates) noexcept;

/// @brief The key a request and its collect present for a candidate list (see `deluge_audio_load_request`).
///
/// Two lists of the same paths in the same order have the same key. Never 0, so 0 can stand for "no list".
///
/// @param candidates    The candidate buffer, laid out as @ref candidates writes it.
/// @param numCandidates How many slots of @p candidates to read.
/// @return The list's key.
[[nodiscard]] uint32_t candidatesKey(const char* candidates, uint32_t numCandidates) noexcept;

/// @brief Open the Sample's streaming read handle: the actual file the audio engine reads clusters
///        from during playback. I/O.
///
/// The synchronous path opens this between allocating the Sample and parsing its header
/// (`SampleStream::open_read_stream`, via `AudioFileManager::buildAudioFileFromCard`). A build
/// published through `finish` WITHOUT this call has its stream handle at 0: it loads with no error,
/// appears wherever loaded samples appear (the browser, a kit row, ...), and plays silence forever,
/// because every cluster read against handle 0 fails cleanly instead of loudly. Call this once, after
/// `begin` and before `finish`.
///
/// This opens a DIFFERENT handle space from whatever efatfs handle the async loader used to read the
/// header: it goes through the `deluge_sample_stream` registry, a scarce resource a browser preview
/// can exhaust (`Error::TOO_MANY_OPEN_STREAMS`) -- on that failure this already retries once after
/// reclaiming unleased streams (`AudioFileManager::releaseUnleasedAudioFiles()`), the same recovery
/// the synchronous path relies on. Do not reimplement that reclaim; call this instead.
///
/// @param build An in-flight build from `begin`.
/// @param path  The path to open: the alternate location the bytes actually live at if the file was
///              resolved there, the nominal path otherwise -- whichever path the resolved size
///              actually came from (matches `begin`'s @p alternatePath).
/// @return `Error::NONE` on success, leaving the handle engaged; otherwise the handle is left
///         disengaged and the build must be abandoned, not finished. `Error::BUG` if @p build is null
///         or already consumed (a caller bug, not a file problem).
[[nodiscard]] Error bindReadStream(SampleBuild* build, std::string_view path) noexcept;

/// @brief As bindReadStream(), opening the file at @p location (DelugeAudioLoadResult::location) rather than looking
///        up a path: no directory walk, and no wait for the filesystem lock behind the storage tier's loads.
/// @return As bindReadStream(); `Error::FILE_NOT_FOUND` also for a location from another mount, after which the caller
///         binds by path.
[[nodiscard]] Error bindReadStreamAt(SampleBuild* build, const DelugeFileLocation& location) noexcept;

/// @brief Write the parsed header geometry onto an in-flight build. No I/O.
///
/// Must be given exactly the fields the synchronous `AudioFile::loadFile` writes onto a Sample, or a
/// sample loaded through the async path renders with the wrong geometry -- silently: it loads without
/// error either way.
///
/// An invalid `raw_data_format` is REFUSED rather than degraded to a default: the synchronous path
/// has no analogue for this failure at all (it assigns a `RawDataFormat` straight from the parser, so
/// an invalid value cannot arise there), and silently accepting one here would be a sample that
/// loads without error and renders wrong. On refusal nothing is written -- the build's geometry is
/// left exactly as it was before this call (zeroed, if this is the first attempt) -- and the caller
/// must `abandon` it rather than proceed to `finish`.
///
/// @param build    An in-flight build from `begin`. A null or already-consumed build is refused.
/// @param geometry The parsed fields.
/// @param error    Written with the outcome (if non-null): `Error::NONE` on success; `Error::BUG` on
///                 any refusal -- @p build null or already consumed, or `geometry.raw_data_format`
///                 not one of `RawDataFormat`'s values. Every refusal here is a caller-contract
///                 violation, not a property of the file (an invalid `raw_data_format` cannot arise
///                 from a well-behaved parser), so all of them share one code.
/// @return true if the geometry was applied; false if it was refused.
[[nodiscard]] bool applyGeometry(SampleBuild* build, const Geometry& geometry, Error* error) noexcept;

/// @brief Apply the parse result, register the fill context, and publish to the cache. No I/O.
///
/// Runs the whole post-parse trailer of `buildAudioFileFromCard`'s SAMPLE path -- there is nothing
/// left for the caller to do afterwards, on success or failure. Always safe against a null or
/// already-consumed @p build (a no-op, returning null). Otherwise always consumes @p build: after
/// this call returns, the pointer is invalid regardless of the result.
///
/// @param build   An in-flight build from `begin`. Consumed if valid.
/// @param errorIn The parse result; `Error::NONE` publishes the build, anything else destroys it
///                (matching `abandon`).
/// @param error   Written with the actual outcome (if non-null): @p errorIn echoed back on a clean
///                publish or a clean destroy, or a DIFFERENT code (e.g. `Error::INSUFFICIENT_RAM`) if
///                publishing itself failed -- a cache-insert failure with @p errorIn == `Error::NONE`
///                must not be reported to the caller as success. `Error::BUG` for an invalid build.
/// @return The published object, or null if the build was destroyed (a non-NONE @p errorIn, a
///         cache-insert failure, or an invalid @p build). The caller keeps this rather than finding
///         the object again by path: finding it again assumes the object is keyed by the path handed
///         to `begin` (which the SYNTH/KIT alternate-location rule does not guarantee), and it
///         searches across the moment the protect-during-setup reason is dropped, when the object is
///         unleased and therefore evictable. Unleased on return, so a caller that keeps it leases it,
///         the same as every other load path.
AudioFile* finish(SampleBuild* build, Error errorIn, Error* error) noexcept;

/// @brief Destroy a build that will never be finished -- a file-open failure, a card pull, or any
///        other path that ends the load before a parse result exists.
///
/// Always safe: a null or already-consumed @p build is a no-op; otherwise this releases the
/// protect-during-setup reason and frees the object, so nothing is left adopted-but-abandoned and no
/// reason leaks. Always consumes a valid @p build; the pointer is invalid after this call returns.
///
/// @param build An in-flight build from `begin`, or null.
void abandon(SampleBuild* build) noexcept;

} // namespace deluge::storage::sample_build
