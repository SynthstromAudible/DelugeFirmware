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
/// @brief The async audio-file loader's request/collect channel, over the C ABI.
///
/// `AudioFileManager::buildAudioFileFromCard`'s SAMPLE path splits into three parts: an I/O-free
/// prologue (allocate + adopt a `Sample`), the I/O (open a candidate, size it, parse its header), and
/// an I/O-free epilogue (apply the parsed geometry, publish). This channel hands the I/O to a Rust
/// async task: the requesting tier asks for a path with its candidate list, returns to its own loop,
/// and collects a plain result on a later tick. The prologue and epilogue around it, and the candidate
/// policy that feeds the request, are the app's own C++ (`storage/audio/sample_build.h`), not part of
/// this ABI.
///
/// **Fixed-width, never a native enum.** Every value crossing this boundary is a fixed-width integer.
/// `arm-none-eabi` defaults to short enums, so a Rust enum's native layout must never be relied on --
/// `Error`-shaped and `RawDataFormat`-shaped values cross as `uint8_t`, and a caller matches on the
/// numeric value rather than reinterpreting the byte as an enum.
#ifndef LIBDELUGE_AUDIO_LOAD_H
#define LIBDELUGE_AUDIO_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "abi.h"
#include "channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Sentinel the app's candidate policy (`deluge::storage::sample_build::candidates`) returns instead of
/// a real count when its buffer is not trustworthy -- a candidate that would have been written did not
/// fit in `DELUGE_PATH_MAX` bytes, or an internal allocation failed. Nobody may read that buffer, and
/// @ref deluge_audio_load_request refuses this value as `num_candidates` rather than clamping it. Never
/// a legitimate count: `AudioFileManager::kMaxLoadCandidates` bounds real returns far below this value.
#define DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED 0xFFFFFFFFu

/// @brief Parsed header geometry, as @ref DelugeAudioLoadResult carries it to the app's build
///        (`deluge::storage::sample_build::applyGeometry`).
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

/* ---------------------------------------------------------------------------
 * The async request/collect pair.
 *
 * The async loader is split so that each half runs where it is allowed to. The Rust task owns
 * only AWAITED file I/O -- open a candidate, read the size, walk the header windows -- and makes
 * no call back into C++ at all. Everything that touches the model (the app's sample build,
 * `storage/audio/sample_build.h`) stays on the C++ tier, run synchronously by the collector once the
 * bytes are already in hand.
 *
 * Two hard constraints force that division, and neither is negotiable:
 *
 *  - The loader task cannot run on the thread-mode executor. It holds the single filesystem mutex
 *    across an SD await, while the C++ `deluge_efatfs_*` entry points bridge to those same futures
 *    through a NON-yielding spin ("the calling context is HELD ... thread-mode tasks do not run
 *    during one" -- storage/efatfs/device.rs). A thread-mode spin would wait forever on a mutex only the task
 *    it is starving can release.
 *  - Running on its own interrupt executor instead, it must never make a synchronous C++ call that
 *    reaches storage, for the mirror-image reason: those calls spin rather than await, and a spin
 *    at interrupt priority cannot be preempted by the thread-mode context holding the mutex. It
 *    must also not allocate through the C++ allocator or mutate model state, which preempting
 *    thread-mode mid-mutation would corrupt.
 *
 * The residue of the second constraint is why `deluge_audio_load_request` takes the candidate list
 * as DATA rather than letting the loader ask for it: the app's candidate policy allocates
 * `std::string`s, and resolving its alternate directory probes one (a spin). Both therefore run on
 * the requesting tier, before the request is made.
 * ------------------------------------------------------------------------- */

/// @brief A finished async header parse: everything the collector needs to build the `Sample`
///        without touching the card again.
///
/// Plain data, no ownership. That is the point rather than an incidental property: a result sits in
/// a fixed-size slot until it is collected, so it must be safe to overwrite or discard one that
/// nobody ever collects. Nothing here owns a file handle, an allocation, or an adopted object, so
/// dropping a result costs exactly the I/O that produced it and leaks nothing.
typedef struct DelugeAudioLoadResult {
	/// The parsed geometry, ready to apply to the app's build. Only meaningful when @ref error is
	/// `Error::NONE`.
	DelugeAudioLoadGeometry geometry;
	/// The resolved on-card size, to start the app's build with.
	uint64_t size_bytes;
	/// The alternate location the bytes were actually found at, or "" if the regular path won -- the
	/// path the app's build opens its read stream on. NUL-terminated.
	char alternate_path[DELUGE_PATH_MAX];
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
	/// The `candidates_key` of the request this answers: the candidate list the verdict was reached with.
	uint32_t candidates_key;
} DelugeAudioLoadResult;

/// @brief Queue @p file_path for the async loader and wake it. Returns immediately; never waits. [task]
///
/// Pair with @ref deluge_audio_load_collect: request, return to your own loop, and ask for the
/// result on a later tick. This is the same non-blocking shape `deluge_streaming_request_chunk`
/// (streaming_fill.h) already uses.
///
/// The candidates are supplied rather than resolved by the loader -- see this section's header
/// comment for why. The app builds them on the calling tier
/// (`deluge::storage::sample_build::candidates`) and passes the buffer straight through.
///
/// @param file_path      The nominal path being loaded, and the key @ref deluge_audio_load_collect
///                       looks the result up by. Must not be NULL.
/// @param candidates     `num_candidates * DELUGE_PATH_MAX` bytes, one NUL-terminated path per slot
///                       in try order. Must not be NULL. Copied out before this returns. A slot holds a
///                       whole path (not a component, as `DELUGE_MAX_FILENAME` does); a candidate that
///                       would not fit is reported by `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED`, never
///                       truncated.
/// @param num_candidates How many slots of @p candidates to read. Anything above
///                       `AudioFileManager::kMaxLoadCandidates` is ignored;
///                       `DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED` is refused.
/// @param candidates_key The requester's key for this candidate list
///                       (`deluge::storage::sample_build::candidatesKey`), opaque to the loader. The answer
///                       is collected only by a collect that presents the same key: two loads of one path
///                       whose candidate lists differ can reach different verdicts, and neither may take the
///                       other's.
/// @return true if the request was accepted. false if it was dropped (the queue is full, the path
///         is not valid UTF-8, or there are no candidates) -- the caller has already been told
///         `LOAD_IN_PROGRESS`, so its next tick re-requests.
bool deluge_audio_load_request(const char* file_path, const char* candidates, uint32_t num_candidates,
                               uint32_t candidates_key) DELUGE_NOEXCEPT;

/// @brief Collect the finished parse for @p file_path made with @p candidates_key's candidates, if one is
///        ready. Never waits. [task]
///
/// On `DELUGE_CHANNEL_READY` the result is written to @p out and removed from the loader, so the
/// caller now owns it and must act on it: build and publish the `Sample` from it (or fail the load
/// with `result.error`). That build is I/O-free apart from opening the Sample's own read stream,
/// because the size and the geometry are already resolved here.
///
/// @param file_path      The nominal path passed to @ref deluge_audio_load_request. Must not be NULL.
/// @param candidates_key The key of the candidate list the caller would request @p file_path with. Only an
///                       answer to a request made with the same key is handed over; another key's answer is
///                       left for its own collector.
/// @param out            Written only when this returns `DELUGE_CHANNEL_READY`. Must not be NULL.
/// @return @ref DELUGE_CHANNEL_PENDING while the path is queued or in flight under any key: ask again next
///         tick, and do NOT re-request. @ref DELUGE_CHANNEL_READY once a result was written to @p out and
///         removed from the loader, so a second collect for the path and key answers UNKNOWN.
///         @ref DELUGE_CHANNEL_UNKNOWN when the loader holds nothing for the path and key -- never requested,
///         already collected, evicted by later loads, or answered only for other candidates -- so request it
///         again.
DelugeChannelStatus deluge_audio_load_collect(const char* file_path, uint32_t candidates_key,
                                              DelugeAudioLoadResult* out) DELUGE_NOEXCEPT;

/// @brief Discard the answer the loader still owes for @p file_path, if it owes one. Never waits. [task]
///
/// For a requester that drops its load with a request still out. Nobody is left to collect that answer, so once this is
/// called it is thrown away when it arrives, or at its collect if it already has, rather than holding a result slot
/// until later loads evict it. The mark is per path, whatever the candidates key: a collect that meets it answers
/// `DELUGE_CHANNEL_UNKNOWN`, so a requester still waiting on the same path asks again and pays one more read. A path
/// with nothing outstanding is left alone, and the next request of a path ends what this did to it.
///
/// @param file_path The nominal path passed to @ref deluge_audio_load_request. Must not be NULL.
void deluge_audio_load_forget(const char* file_path) DELUGE_NOEXCEPT;

/* Discarding what this loader parsed from a card that has been ejected is deluge_card_flush
 * (card.h), not a call of this header's own: the ejection invalidates every loader at once, and
 * they share one card generation to say so. Deliberately not a doc comment -- it documents an
 * absence, and would otherwise attach itself to the declaration below. */

#ifdef DELUGE_HOST
/// @brief Host harness only: make every audio load wait @p ms milliseconds after it is marked in flight. [task]
///
/// Lets a gate hold a load in flight while a consumer keeps collecting PENDING. 0 (the default) means no wait.
/// Declared only for host builds, and defined only by the Rust BSP's `host_app` build; never in a device image.
/// @param ms Milliseconds of simulated delay per load.
void deluge_audio_load_set_latency_ms(uint32_t ms) DELUGE_NOEXCEPT;

/// @brief Host harness only: how much the loader holds for @p file_path, under any candidates key. [task]
///
/// Counts the path's queued requests, its in-flight load and its published answers, so a gate can see that nothing
/// is left for a path whatever candidates it was asked with. A table the loader holds at that moment counts as one
/// entry. Declared only for host builds, and defined only by the Rust BSP's `host_app` build.
/// @param file_path The nominal path. Must not be NULL.
/// @return The number of entries held for the path.
uint32_t deluge_audio_load_held_for_path(const char* file_path) DELUGE_NOEXCEPT;
#endif

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_AUDIO_LOAD_H
