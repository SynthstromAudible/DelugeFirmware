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
/// **Call order.** The candidate policy comes FIRST and on a different tier from the parse, because
/// it is neither I/O-free nor allowed on the loader's executor:
///
///   `candidates` (and, only where a load has already missed, `resolve_alternate_dir` before it)
///   -> `request` -> the caller returns to its own loop -> `collect`, on a later tick, until it
///   reports READY -> `begin` -> `bind_read_stream` on the path the result names -> `apply_geometry`
///   -> `finish` (or `abandon` at any point after `begin`, including when `bind_read_stream` or
///   `apply_geometry` itself fails).
///
/// `begin` is deliberately LAST-minute rather than first: it adopts and reasons a `Sample`, so
/// opening a build before the I/O would mean owning one across every await. Nothing is adopted until
/// the bytes are already in hand.
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

/* Not currently parsed as C: as of this writing neither wrapper.h nor the libdeluge.h umbrella
 * includes this header (src/bsp/rust/build.rs), so only the two C++ translation units that include
 * it directly (audio_load_abi.cpp, host_loadcheck_main.cpp) ever see it. This macro is forward-
 * looking hardening, not a fix for an active failure: it matches the C-parseable convention the
 * sibling libdeluge headers (audio_format.h, file_io.h) already follow, for whenever a Rust consumer
 * of this ABI is wired in (a later migration step) and this header joins wrapper.h. `noexcept` is
 * C++-only and invalid C syntax, so it is spelled through this macro -- #undef'd at the end of the
 * file so it cannot leak into (or collide with a same-named macro in) whatever includes this header
 * next -- rather than written bare. */
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

/// @brief A published `AudioFile`. Opaque, and owned by `AudioFileManager::audioFiles` -- never by the
///        caller: this is a borrowed view of the object `deluge_audio_load_finish` inserted, valid
///        exactly as long as that object stays resident (it is unleased at that moment, so a caller
///        that intends to keep it leases it first, the same as every other load path).
///
/// Exists so `finish` can hand its caller the object it published rather than making the caller find
/// it again by path. Nothing outside C++ has any use for it -- a non-C++ caller passes NULL and
/// ignores it -- and only C++ may cast it back (to `AudioFile*`).
typedef struct DelugeAudioFile DelugeAudioFile;

/// @brief Allocate and construct a `Sample` from an already-resolved size. No I/O.
///
/// Adopts the object and gives it a protect-during-setup reason before returning, so from here the
/// caller owns its fate unconditionally -- see this file's ownership note. Validates `size_bytes`
/// itself (`FILE_CORRUPTED` for 0, `FILE_TOO_BIG` over the 1 GB ceiling); the caller does not need to
/// re-check either.
///
/// @param file_path       The file's nominal/display path. Must not be NULL.
/// @param alternate_path  The alternate-load-dir path segment the bytes actually live at, or an empty
///                        string if resolved at @p file_path directly. Must not be NULL (pass "" for
///                        "none", not NULL).
/// @param size_bytes      The resolved on-card file's size, in bytes.
/// @param error_out       Written with a Deluge `Error` code (as `uint8_t`) on failure. May be NULL if
///                        the caller does not need the code (a NULL build is still NULL on failure).
/// @return The new build, or NULL on failure (nothing adopted, no reason held) with @p error_out set
///         (if non-NULL).
DelugeSampleBuild* deluge_audio_load_begin(const char* file_path, const char* alternate_path, uint64_t size_bytes,
                                           uint8_t* error_out) DELUGE_NOEXCEPT;

/// @brief Resolve whether the alternate load dir exists, before proposing candidates.
///
/// I/O: opens the alternate directory. A song/preset load starts with the alternate-dir status
/// unresolved (`MIGHT_EXIST`), in which state `deluge_audio_load_candidates` proposes only the regular
/// path -- promoting that status to `DOES_EXIST`/`NOT_FOUND` is what unlocks the alternate-dir
/// candidates, and it is I/O, so it cannot live in the (I/O-free) candidates call itself.
///
/// **Call this AFTER a load has missed, never before its first `deluge_audio_load_candidates`.** The
/// tempting reading -- "resolve first so the candidate list is complete" -- is the defect this
/// wording replaced, and re-introducing it is a behaviour change for loads that never touch this
/// path at all. `alternateLoadDirStatus` is per-LOAD shared state: promoting it up front makes every
/// LATER sample of the same load try the alternate directory first, including the ones taking the
/// synchronous path, which still promotes only after a real miss. So the load's effective try order
/// flips out from under callers this function never runs for.
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
/// @param error_out Written unconditionally (if non-NULL) with a Deluge `Error` code (as `uint8_t`):
///                  `Error::NONE` whenever this returns true (already resolved, or newly resolved as
///                  present); `Error::FILE_UNREADABLE` if the probe finds the dir genuinely absent, or
///                  `Error::SD_CARD` if presence couldn't be determined, when it returns false. May be
///                  NULL if the caller does not need the code.
/// @return true once the alternate dir is known to exist (already, or newly resolved); false
///         otherwise.
bool deluge_audio_load_resolve_alternate_dir(uint8_t* error_out) DELUGE_NOEXCEPT;

/// @brief Write the candidate paths for @p file_path, in try order, into @p out. No I/O -- alternate-
///        load-dir policy only, including the SYNTH/KIT naming rules and the bare-filename fallback.
///
/// Call `deluge_audio_load_resolve_alternate_dir` first (see its doc comment) or this proposes only
/// the regular path even when an alternate dir exists.
///
/// @param file_path      The nominal path a load was asked for. Must not be NULL.
/// @param out            Caller-owned buffer of at least `max_candidates * DELUGE_MAX_PATH` bytes; each
///                       slot holds one NUL-terminated path, in try order. Must not be NULL.
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
///              actually came from (matches @ref deluge_audio_load_begin's `alternate_path`). Must not
///              be NULL.
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
/// Unlike the other setters in this header, an invalid `raw_data_format` is REFUSED rather than
/// degraded to a default: the synchronous path has no analogue for this failure at all (it assigns a
/// `RawDataFormat` straight from the parser, so an invalid value cannot arise there), and silently
/// accepting one here would be exactly the "loads without error, renders wrong" failure this file's
/// own doc comment calls the worst available shape. On refusal nothing is written -- the build's
/// geometry is left exactly as it was before this call (zeroed, if this is the first attempt) -- and
/// the caller must `deluge_audio_load_abandon` it rather than proceed to `deluge_audio_load_finish`.
///
/// @param build     An in-flight build from deluge_audio_load_begin. A NULL or already-consumed build
///                  fails (see @p error_out) rather than silently no-opping.
/// @param geometry  The parsed fields. A NULL @p geometry fails the same way. Must not be NULL.
/// @param error_out Written with a Deluge `Error` code (as `uint8_t`): `Error::NONE` on success;
///                  `Error::BUG` on any refusal -- @p build or @p geometry NULL, @p build already
///                  consumed, or `geometry->raw_data_format` not one of `RawDataFormat`'s values.
///                  Every refusal here is a caller-contract violation, not a property of the file (an
///                  invalid `raw_data_format` cannot arise from a well-behaved parser -- see above),
///                  so all three share one code rather than pretending to distinguish a file problem
///                  that cannot occur. May be NULL if the caller does not need the code (the return
///                  value alone still distinguishes success from refusal).
/// @return true if the geometry was applied; false if it was refused (see @p error_out).
bool deluge_audio_load_apply_geometry(DelugeSampleBuild* build, const DelugeAudioLoadGeometry* geometry,
                                      uint8_t* error_out) DELUGE_NOEXCEPT;

/// @brief Apply the parse result, register the fill context, and publish to the cache. No I/O.
///
/// Runs the entire trailer `buildAudioFileFromCard`'s synchronous SAMPLE path used to run after a
/// successful parse -- there is nothing left for the caller to do afterwards, on success or failure.
/// Always safe against a NULL or already-consumed @p build (a no-op, returning false). Otherwise
/// always consumes @p build: after this call returns, the pointer is invalid regardless of the
/// result.
///
/// @param build         An in-flight build from deluge_audio_load_begin. Consumed if valid.
/// @param error_in      The parse result (a Deluge `Error` code, as `uint8_t`); `Error::NONE`
///                      publishes the build, anything else destroys it (matching @ref
///                      deluge_audio_load_abandon).
/// @param error_out     Written with the actual outcome (a Deluge `Error` code, as `uint8_t`): @p
///                      error_in echoed back on a clean publish or a clean destroy, or a DIFFERENT
///                      code (e.g. `Error::INSUFFICIENT_RAM`) if publishing itself failed -- a
///                      cache-insert failure with @p error_in == `Error::NONE` must not be reported
///                      to the caller as success. May be NULL if the caller does not need the
///                      distinction.
/// @param published_out Written with the published object whenever this returns true, and with NULL
///                      whenever it returns false -- so the caller never has to find the object again
///                      by path. Finding it again is not equivalent: it assumes the object is keyed by
///                      the path handed to @ref deluge_audio_load_begin (which the SYNTH/KIT
///                      alternate-location rule does not guarantee), and it searches across the moment
///                      the protect-during-setup reason is dropped, when the object is unleased and
///                      therefore evictable. May be NULL if the caller does not want the object (a
///                      non-C++ caller has no use for it).
/// @return true if the build was published; false if it was destroyed (a non-NONE @p error_in, a
///         cache-insert failure, or an invalid @p build).
bool deluge_audio_load_finish(DelugeSampleBuild* build, uint8_t error_in, uint8_t* error_out,
                              DelugeAudioFile** published_out) DELUGE_NOEXCEPT;

/// @brief Destroy a build that will never be finished -- a file-open failure, a card pull, or any
///        other path that ends the load before a parse result exists.
///
/// Always safe: a NULL or already-consumed @p build is a no-op; otherwise this releases the
/// protect-during-setup reason and frees the object, so nothing is left adopted-but-abandoned and no
/// reason leaks. Always consumes a valid @p build; the pointer is invalid after this call returns.
///
/// @param build An in-flight build from deluge_audio_load_begin, or NULL.
void deluge_audio_load_abandon(DelugeSampleBuild* build) DELUGE_NOEXCEPT;

/* ---------------------------------------------------------------------------
 * The async request/collect pair.
 *
 * The async loader is split so that each half runs where it is allowed to. The Rust task owns
 * only AWAITED file I/O -- open a candidate, read the size, walk the header windows -- and makes
 * no call back into C++ at all. Everything that touches the model (`begin`, `bind_read_stream`,
 * `apply_geometry`, `finish`) stays on the C++ tier, run synchronously by the collector once the
 * bytes are already in hand.
 *
 * Two hard constraints force that division, and neither is negotiable:
 *
 *  - The loader task cannot run on the thread-mode executor. It holds the single filesystem mutex
 *    across an SD await, while the C++ `deluge_efatfs_*` entry points bridge to those same futures
 *    through a NON-yielding spin ("the calling context is HELD ... thread-mode tasks do not run
 *    during one" -- efatfs_fs.rs). A thread-mode spin would wait forever on a mutex only the task
 *    it is starving can release.
 *  - Running on its own interrupt executor instead, it must never make a synchronous C++ call that
 *    reaches storage, for the mirror-image reason: those calls spin rather than await, and a spin
 *    at interrupt priority cannot be preempted by the thread-mode context holding the mutex. It
 *    must also not allocate through the C++ allocator or mutate model state, which preempting
 *    thread-mode mid-mutation would corrupt.
 *
 * The residue of the second constraint is why `deluge_audio_load_request` takes the candidate list
 * as DATA rather than letting the loader ask for it: `deluge_audio_load_resolve_alternate_dir`
 * probes a directory (a spin), and `deluge_audio_load_candidates` allocates `std::string`s. Both
 * therefore run on the requesting tier, before the request is made.
 * ------------------------------------------------------------------------- */

/// @brief A finished async header parse: everything the collector needs to build the `Sample`
///        without touching the card again.
///
/// Plain data, no ownership. That is the point rather than an incidental property: a result sits in
/// a fixed-size slot until it is collected, so it must be safe to overwrite or discard one that
/// nobody ever collects. Nothing here owns a file handle, an allocation, or an adopted object, so
/// dropping a result costs exactly the I/O that produced it and leaks nothing.
typedef struct DelugeAudioLoadResult {
	/// The parsed geometry, ready to hand to @ref deluge_audio_load_apply_geometry. Only meaningful
	/// when @ref error is `Error::NONE`.
	DelugeAudioLoadGeometry geometry;
	/// The resolved on-card size, to hand to @ref deluge_audio_load_begin.
	uint64_t size_bytes;
	/// The alternate location the bytes were actually found at, or "" if the regular path won --
	/// exactly @ref deluge_audio_load_begin's `alternate_path`, and the path
	/// @ref deluge_audio_load_bind_read_stream must open. NUL-terminated.
	char alternate_path[DELUGE_MAX_PATH];
	/// A Deluge `Error` code (as `uint8_t`). `Error::NONE` means @ref geometry is valid and the
	/// build should be published; anything else is the loader's verdict to fail the load with, and
	/// carries the same identity the synchronous path produced (`FILE_UNSUPPORTED` for "not audio"
	/// or an unplayable format, `FILE_CORRUPTED` for malformed or truncated, `FILE_UNREADABLE` for
	/// "no candidate opened").
	///
	/// `TOO_MANY_OPEN_STREAMS` is the one verdict that is NOT about the file: the efatfs handle
	/// table had no free slot, so nothing was even attempted. It is reported under its own identity
	/// rather than as `FILE_UNREADABLE` precisely because the two need different recoveries -- a
	/// miss earns the alternate-directory probe, a full table earns
	/// `AudioFileManager::releaseUnleasedAudioFiles()` and one retry, which is what the synchronous
	/// path does (`SampleStream::open_read_stream`). Collapsing them sent a full table probing the
	/// alternate directory and answering with a different file.
	uint8_t error;
} DelugeAudioLoadResult;

/// @ref deluge_audio_load_collect: the loader has no result for this path -- it is queued, or in
/// flight. Ask again next tick; do NOT re-request.
#define DELUGE_AUDIO_LOAD_PENDING 0u
/// @ref deluge_audio_load_collect: a result was written to `out` and REMOVED from the loader. A
/// second collect for the same path reports `DELUGE_AUDIO_LOAD_UNKNOWN`.
#define DELUGE_AUDIO_LOAD_READY 1u
/// @ref deluge_audio_load_collect: the loader knows nothing about this path. Either it was never
/// requested, its result was already collected, or its result was evicted by later loads. The
/// caller should request it (again).
#define DELUGE_AUDIO_LOAD_UNKNOWN 2u

/// @brief Queue @p file_path for the async loader and wake it. Returns immediately; never waits.
///
/// Pair with @ref deluge_audio_load_collect: request, return to your own loop, and ask for the
/// result on a later tick. This is the same non-blocking shape
/// `deluge_streaming_request_chunk`/`deluge_streaming_chunk_ready` (streaming_fill.h) already uses.
///
/// The candidates are supplied rather than resolved by the loader -- see this section's header
/// comment for why. Call `deluge_audio_load_resolve_alternate_dir` and then
/// `deluge_audio_load_candidates` on the calling tier and pass the buffer straight through.
///
/// @param file_path      The nominal path being loaded, and the key @ref deluge_audio_load_collect
///                       looks the result up by. Must not be NULL.
/// @param candidates     `num_candidates * DELUGE_MAX_PATH` bytes, one NUL-terminated path per slot
///                       in try order -- exactly the buffer `deluge_audio_load_candidates` filled.
///                       Must not be NULL. Copied out before this returns.
/// @param num_candidates How many slots of @p candidates to read. Anything above
///                       `AudioFileManager::kMaxLoadCandidates` is ignored.
/// @return true if the request was accepted. false if it was dropped (the queue is full, the path
///         is not valid UTF-8, or there are no candidates) -- the caller has already been told
///         `LOAD_IN_PROGRESS`, so its next tick re-requests.
bool deluge_audio_load_request(const char* file_path, const char* candidates, uint32_t num_candidates) DELUGE_NOEXCEPT;

/// @brief Collect the finished parse for @p file_path, if one is ready. Never waits.
///
/// On `DELUGE_AUDIO_LOAD_READY` the result is written to @p out and removed from the loader, so the
/// caller now owns it and must act on it: run `begin` -> `bind_read_stream` -> `apply_geometry` ->
/// `finish` (or fail the load with `result.error`). All of that is I/O-free apart from
/// `bind_read_stream`'s own open, because the size and the geometry are already resolved here.
///
/// @param file_path The nominal path passed to @ref deluge_audio_load_request. Must not be NULL.
/// @param out       Written only when this returns `DELUGE_AUDIO_LOAD_READY`. Must not be NULL.
/// @return One of `DELUGE_AUDIO_LOAD_PENDING` / `_READY` / `_UNKNOWN`.
uint8_t deluge_audio_load_collect(const char* file_path, DelugeAudioLoadResult* out) DELUGE_NOEXCEPT;

/// @brief Discard every published result, queued request and in-flight mark. Never waits.
///
/// Call this when the card is EJECTED. Everything the loader holds is keyed by the nominal path and
/// nothing records which card produced it, so a result parsed from one card must never be collected
/// after another has been inserted: @ref deluge_audio_load_bind_read_stream would open the NEW
/// card's file at that path and @ref deluge_audio_load_apply_geometry would write the OLD card's
/// numbers onto it. That loads with no error and plays the wrong thing -- this header's own "worst
/// available shape".
///
/// **At ejection, not at re-insertion.** Flushing on the way back in leaves the window open for as
/// long as the slot stays empty, and the re-insert path (`deluge_efatfs_remount` ->
/// `AudioFileManager::firstCardRead` -> `cardReinserted`) is about re-validating RESIDENT samples,
/// which is a different question from what the loader has in its tables.
///
/// A load that is genuinely in flight when this is called is also covered: it publishes after this
/// returns, and its result is discarded rather than reinstated. The loader stamps each result with
/// the card generation the load began under and re-checks that stamp when the result is COLLECTED,
/// so the discard does not depend on this call having managed to empty anything, nor on which
/// context either side runs in. There is nothing for the caller to sequence against and nothing
/// left in the loader to clean up afterwards.
///
/// **That is a statement about the loader's tables, not about the whole load pipeline.** A result
/// already handed out is past the loader's reach: @ref deluge_audio_load_collect removes it, and the
/// caller then runs @ref deluge_audio_load_begin -> @ref deluge_audio_load_bind_read_stream -> @ref
/// deluge_audio_load_apply_geometry, of which the bind is a real open that can yield the storage
/// owner. An ejection landing in that window has nothing left to flush -- the geometry is already on
/// the caller's stack. It degrades to a read error rather than a wrong load, because the re-insert
/// (`AudioFileManager::reinitEjectedCard` -> `deluge_efatfs_remount`) is dispatched to that same
/// owner through the storage `Coalescer` and so cannot run during the bind: the open hits the old
/// mount and fails.
///
/// Afterwards every flushed path reports @ref DELUGE_AUDIO_LOAD_UNKNOWN, whose documented meaning is
/// already "request it (again)" -- the collector needs no new lifecycle for this.
void deluge_audio_load_flush(void) DELUGE_NOEXCEPT;

/// @brief Whether the Rust async loader backs sample loads on this BSP/config.
/// @return true if async loading is active; false if every load takes the synchronous path.
bool deluge_audio_load_async_active(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#undef DELUGE_NOEXCEPT
#endif // LIBDELUGE_AUDIO_LOAD_H
