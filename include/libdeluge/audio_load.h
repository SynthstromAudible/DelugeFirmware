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
/// @brief The card-sample build's prologue/epilogue, over the C ABI.
///
/// `AudioFileManager::buildAudioFileFromCard`'s SAMPLE path splits into three parts: an I/O-free
/// prologue (allocate + adopt a `Sample`), the I/O (open the alternate dir, open the read stream,
/// open the file, parse its header), and an I/O-free epilogue (apply the parsed geometry, publish).
/// This boundary exposes the I/O-free halves plus the read-stream bind so a Rust async task can own
/// the file-open-and-parse I/O in between -- that part moves off the calling fiber without moving the
/// allocation/bookkeeping with it.
///
/// **Call order.** `begin` -> (optionally, repeatedly) `resolve_alternate_dir` + `candidates` to pick
/// a path -> `bind_read_stream` on the chosen path -> the caller's own I/O to read and parse the
/// header -> `apply_geometry` -> `finish` (or `abandon` at any point after `begin`).
///
/// **Ownership.** `deluge_audio_load_begin` allocates and adopts a `Sample` and gives it a
/// protect-during-setup reason BEFORE any I/O runs. From the moment it returns a non-NULL build,
/// exactly one of `deluge_audio_load_finish` or `deluge_audio_load_abandon` MUST be called on it --
/// on every path: a clean parse, a parse error, a file-open failure, or the card being pulled
/// mid-load. A build that is never finished or abandoned sits in `AudioFileManager::audioFiles`
/// adopted, reasoned (so it can never be evicted), and still at zero geometry, which makes every
/// fill-context consumer that later looks it up silently resolve nothing for that sample.
///
/// Separately, `deluge_audio_load_bind_read_stream` MUST be called on every build that reaches
/// `deluge_audio_load_finish` -- see its own doc comment for what a published build without one
/// actually does (nothing catches it; it just plays silence).
///
/// **Fixed-width, never a native enum.** Every value crossing this boundary is a fixed-width integer.
/// `arm-none-eabi` defaults to short enums, so a Rust enum's native layout must never be relied on --
/// `Error`-shaped and `RawDataFormat`-shaped values cross as `uint8_t`, and a caller matches on the
/// numeric value rather than reinterpreting the byte as an enum.
#ifndef LIBDELUGE_AUDIO_LOAD_H
#define LIBDELUGE_AUDIO_LOAD_H

#include <stdbool.h>
#include <stdint.h>

/* This header is parsed as plain C by bindgen (wrapper.h, src/bsp/rust/build.rs) as well as compiled
 * as C++, so `noexcept` -- meaningful only C++-side, and invalid C syntax -- is spelled through this
 * macro rather than written bare. */
#ifdef __cplusplus
#define DELUGE_NOEXCEPT noexcept
extern "C" {
#else
#define DELUGE_NOEXCEPT
#endif

/// Longest single candidate path `deluge_audio_load_candidates` will write into one slot of its
/// caller-provided buffer, including the NUL terminator. Deliberately NOT the same bound as
/// `DELUGE_MAX_FILENAME` (file_io.h): that one is a single path COMPONENT (FAT LFN max 255 chars),
/// while a load candidate is a full multi-segment path (e.g. an alternate-dir "collect media"
/// flattened name). Matches this codebase's existing whole-path ceiling instead
/// (`kFilenameBufferSize`, definitions_cxx.hpp) -- the bound the storage/XML layer already treats as
/// the longest path it will read or write, so no load candidate legitimately needs more. A candidate
/// that would not fit is reported via `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED`, never silently
/// truncated by `snprintf`.
#define DELUGE_MAX_PATH 256u

/// Sentinel `deluge_audio_load_candidates` returns instead of a real count when `out` is not
/// trustworthy -- a candidate that would have been written did not fit in `DELUGE_MAX_PATH` bytes, or
/// an internal allocation failed. The caller must not read `out` and must fail the load rather than
/// open whatever a silent truncation would have left there. Never a legitimate count:
/// `AudioFileManager::kMaxLoadCandidates` bounds real returns far below this value.
#define DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED 0xFFFFFFFFu

/// @brief A card-sample build in flight. Opaque; wraps the `Sample*` `beginSampleFromCard` allocated
///        plus the bookkeeping `finish`/`abandon` need to close it out. Owned exactly as documented on
///        `deluge_audio_load_begin`.
typedef struct DelugeSampleBuild DelugeSampleBuild;

/// @brief Allocate and construct a `Sample` from an already-resolved size. No I/O.
///
/// Adopts the object and gives it a protect-during-setup reason before returning, so from here the
/// caller owns its fate unconditionally -- see this file's ownership note. Validates `size_bytes`
/// itself (`FILE_CORRUPTED` for 0, `FILE_TOO_BIG` over the 1 GB ceiling); the caller does not need to
/// re-check either.
///
/// @param file_path       The file's nominal/display path.
/// @param alternate_path  The alternate-load-dir path segment the bytes actually live at, or an empty
///                        string if resolved at @p file_path directly.
/// @param size_bytes      The resolved on-card file's size, in bytes.
/// @param error_out       Written with a Deluge `Error` code (as `uint8_t`) on failure. Must not be
///                        NULL.
/// @return The new build, or NULL on failure (nothing adopted, no reason held) with @p error_out set.
DelugeSampleBuild* deluge_audio_load_begin(const char* file_path, const char* alternate_path, uint64_t size_bytes,
                                           uint8_t* error_out) DELUGE_NOEXCEPT;

/// @brief Resolve whether the alternate load dir exists, before proposing candidates.
///
/// I/O: opens the alternate directory. A song/preset load starts with the alternate-dir status
/// unresolved (`MIGHT_EXIST`), in which state `deluge_audio_load_candidates` proposes only the regular
/// path -- promoting that status to `DOES_EXIST`/`NOT_FOUND` is what unlocks the alternate-dir
/// candidates, and it is I/O, so it cannot live in the (I/O-free) candidates call itself. The async
/// loader must call this and let it settle before its first `deluge_audio_load_candidates` of a load,
/// or a collect-media song's first sample silently misses candidates the synchronous path would have
/// found.
///
/// A no-op, doing no I/O, once the status is already resolved (from an earlier call in the same
/// load, or because nothing is loading) -- safe to call speculatively.
///
/// @param error_out Written unconditionally with a Deluge `Error` code (as `uint8_t`):
///                  `Error::NONE` whenever this returns true (already resolved, or newly resolved as
///                  present); `Error::FILE_UNREADABLE` if the probe finds the dir genuinely absent, or
///                  `Error::SD_CARD` if presence couldn't be determined, when it returns false. Must
///                  not be NULL.
/// @return true once the alternate dir is known to exist (already, or newly resolved); false
///         otherwise.
bool deluge_audio_load_resolve_alternate_dir(uint8_t* error_out) DELUGE_NOEXCEPT;

/// @brief Write the candidate paths for @p file_path, in try order, into @p out. No I/O -- alternate-
///        load-dir policy only, including the SYNTH/KIT naming rules and the bare-filename fallback.
///
/// Call `deluge_audio_load_resolve_alternate_dir` first (see its doc comment) or this proposes only
/// the regular path even when an alternate dir exists.
///
/// @param file_path      The nominal path a load was asked for.
/// @param out            Caller-owned buffer of at least `max_candidates * DELUGE_MAX_PATH` bytes; each
///                       slot holds one NUL-terminated path, in try order.
/// @param max_candidates Capacity of @p out, in slots.
/// @return How many candidates were proposed in total. This can exceed @p max_candidates, in which case
///         only the first @p max_candidates were written to @p out -- the same snprintf-style
///         return-vs-capacity contract as the C++ side (`AudioFileManager::proposeLoadCandidates`).
///         `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED` if @p out is not trustworthy at all (a candidate
///         that would have been written did not fit in `DELUGE_MAX_PATH` bytes, or an internal
///         allocation failed) -- the caller must not read @p out in that case.
uint32_t deluge_audio_load_candidates(const char* file_path, char* out, uint32_t max_candidates) DELUGE_NOEXCEPT;

/// @brief Open the Sample's streaming read handle: the actual file the audio engine reads clusters
///        from during playback. I/O.
///
/// The synchronous path opens this between allocating the Sample and parsing its header
/// (`SampleStream::open_read_stream`, via `AudioFileManager::buildAudioFileFromCard`). A build
/// published through `deluge_audio_load_finish` WITHOUT this call has its stream handle at 0: it
/// loads with no error, appears wherever loaded samples appear (the browser, a kit row, ...), and
/// plays silence forever, because every cluster read against handle 0 fails cleanly instead of
/// loudly. Call this once, after `deluge_audio_load_begin` and before `deluge_audio_load_finish`.
///
/// This opens a DIFFERENT handle space from whatever efatfs handle the async loader used to read the
/// header: it goes through the `deluge_sample_stream` registry, a scarce resource a browser preview
/// can exhaust (`Error::TOO_MANY_OPEN_STREAMS`) -- on that failure this already retries once after
/// reclaiming unleased streams (`AudioFileManager::releaseUnleasedAudioFiles()`), the same recovery
/// the synchronous path relies on. Do not reimplement that reclaim; call this instead.
///
/// @param build An in-flight build from deluge_audio_load_begin.
/// @param path  The path to open: the alternate location the bytes actually live at if the file was
///              resolved there, the nominal path otherwise -- whichever path the resolved size
///              actually came from (matches @ref deluge_audio_load_begin's `alternate_path`).
/// @return A Deluge `Error` code (as `uint8_t`): `Error::NONE` on success, leaving the handle
///         engaged; otherwise the handle is left disengaged and the build must be abandoned, not
///         finished. `Error::BUG` if @p build is NULL or already consumed (a caller bug, not a file
///         problem).
uint8_t deluge_audio_load_bind_read_stream(DelugeSampleBuild* build, const char* path) DELUGE_NOEXCEPT;

/// @brief Parsed header geometry to apply to an in-flight build via
///        `deluge_audio_load_apply_geometry`.
///
/// A named-field struct, not positional scalars: the ABI this replaced passed the same information
/// as 12 adjacent parameters, several of them same-typed and same-width, where a caller-side
/// transposition (e.g. swapping `audio_data_start_pos_bytes` and `file_loop_start_samples`, both
/// `uint32_t`) compiled clean and rendered wrong -- the worst failure shape this boundary can produce.
typedef struct DelugeAudioLoadGeometry {
	uint32_t sample_rate;                ///< Sample rate, Hz.
	uint32_t audio_data_start_pos_bytes; ///< Offset from the start of the file to the first audio byte.
	uint32_t audio_data_length_bytes;    ///< Length of the audio data, in bytes.
	uint32_t file_loop_start_samples;    ///< 0 == no loop (the file format's own convention).
	uint32_t file_loop_end_samples;      ///< 0 == no loop (the file format's own convention).
	/// Samples per cycle, carried in case a later Sample->WaveTable conversion needs the file's hint.
	uint32_t wavetable_cycle_size;
	/// The file's declared root note. Only meaningful when `midi_note_present` is nonzero; follows
	/// the file's own "unspecified" convention (mirrors the -1.0f sentinel `Sample::midiNoteFromFile`
	/// uses for the same fact).
	float midi_note_value;
	uint8_t num_channels; ///< Channel count.
	uint8_t byte_depth;   ///< Bytes per sample word.
	/// Matches `RawDataFormat`'s value. Never cast a foreign byte into the C++ enum type -- match on
	/// the number.
	uint8_t raw_data_format;
	uint8_t midi_note_present;                   ///< 0 == the file specified no root note.
	uint8_t wavetable_explicitly_specifies_self; ///< Nonzero if a `clm ` tag declared the file a wavetable.
} DelugeAudioLoadGeometry;

/// @brief Write the parsed header geometry onto an in-flight build. No I/O.
///
/// Must be given exactly the fields the synchronous `AudioFile::loadFile` writes onto a Sample, or a
/// sample loaded through the async path renders with the wrong geometry -- silently: it loads without
/// error either way.
///
/// @param build    An in-flight build from deluge_audio_load_begin. A NULL or already-consumed build
///                 is a silent no-op.
/// @param geometry The parsed fields. A NULL @p geometry is a no-op.
void deluge_audio_load_apply_geometry(DelugeSampleBuild* build,
                                      const DelugeAudioLoadGeometry* geometry) DELUGE_NOEXCEPT;

/// @brief Apply the parse result, register the fill context, and publish to the cache. No I/O.
///
/// Runs the entire trailer `buildAudioFileFromCard`'s synchronous SAMPLE path used to run after a
/// successful parse -- there is nothing left for the caller to do afterwards, on success or failure.
/// Always safe against a NULL or already-consumed @p build (a no-op, returning false). Otherwise
/// always consumes @p build: after this call returns, the pointer is invalid regardless of the
/// result.
///
/// @param build     An in-flight build from deluge_audio_load_begin. Consumed if valid.
/// @param error_in  The parse result (a Deluge `Error` code, as `uint8_t`); `Error::NONE` publishes
///                  the build, anything else destroys it (matching @ref deluge_audio_load_abandon).
/// @param error_out Written with the actual outcome (a Deluge `Error` code, as `uint8_t`): @p
///                  error_in echoed back on a clean publish or a clean destroy, or a DIFFERENT code
///                  (e.g. `Error::INSUFFICIENT_RAM`) if publishing itself failed -- a cache-insert
///                  failure with @p error_in == `Error::NONE` must not be reported to the caller as
///                  success. May be NULL if the caller does not need the distinction.
/// @return true if the build was published; false if it was destroyed (a non-NONE @p error_in, a
///         cache-insert failure, or an invalid @p build).
bool deluge_audio_load_finish(DelugeSampleBuild* build, uint8_t error_in, uint8_t* error_out) DELUGE_NOEXCEPT;

/// @brief Destroy a build that will never be finished -- a file-open failure, a card pull, or any
///        other path that ends the load before a parse result exists.
///
/// Always safe: a NULL or already-consumed @p build is a no-op; otherwise this releases the
/// protect-during-setup reason and frees the object, so nothing is left adopted-but-abandoned and no
/// reason leaks. Always consumes a valid @p build; the pointer is invalid after this call returns.
///
/// @param build An in-flight build from deluge_audio_load_begin, or NULL.
void deluge_audio_load_abandon(DelugeSampleBuild* build) DELUGE_NOEXCEPT;

/// @brief Queue @p file_path for the async loader and wake it.
///
/// Takes only the path: the loader resolves candidates, opens one, and reads its size itself. Silently
/// drops the request if the queue is full -- the caller has already been told `LOAD_IN_PROGRESS`, and
/// the next cursor move re-requests.
///
/// @param file_path The nominal path to load.
void deluge_audio_load_enqueue(const char* file_path) DELUGE_NOEXCEPT;

/// @brief Whether the Rust async loader backs sample loads on this BSP/config.
/// @return true if async loading is active; false if every load takes the synchronous path.
bool deluge_audio_load_async_active(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_AUDIO_LOAD_H
