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
/// prologue (allocate + adopt a `Sample`), the I/O (open the alternate dir, open the file, parse its
/// header), and an I/O-free epilogue (apply the parsed geometry, publish). This boundary exposes the
/// two I/O-free halves so a Rust async task can own the I/O in between -- the file open and header
/// parse move off the calling fiber without moving the allocation/bookkeeping with them.
///
/// **Ownership.** `deluge_audio_load_begin` allocates and adopts a `Sample` and gives it a
/// protect-during-setup reason BEFORE any I/O runs. From the moment it returns a non-NULL build,
/// exactly one of `deluge_audio_load_finish` or `deluge_audio_load_abandon` MUST be called on it --
/// on every path: a clean parse, a parse error, a file-open failure, or the card being pulled
/// mid-load. A build that is never finished or abandoned sits in `AudioFileManager::audioFiles`
/// adopted, reasoned (so it can never be evicted), and still at zero geometry, which makes every
/// fill-context consumer that later looks it up silently resolve nothing for that sample.
///
/// **Fixed-width, never a native enum.** Every value crossing this boundary is a fixed-width integer.
/// `arm-none-eabi` defaults to short enums, so a Rust enum's native layout must never be relied on --
/// `goal`-shaped and `Error`-shaped values cross as `uint8_t`, and a caller matches on the numeric
/// value rather than reinterpreting the byte as an enum.
#ifndef LIBDELUGE_AUDIO_LOAD_H
#define LIBDELUGE_AUDIO_LOAD_H

#include "file_io.h" // DELUGE_MAX_FILENAME
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Longest single candidate path `deluge_audio_load_candidates` will write into one slot of its
/// caller-provided buffer, including the NUL terminator. Shares its value with `DELUGE_MAX_FILENAME`
/// (file_io.h) -- a load candidate is a path on the same storage volume, so the same bound applies.
#define DELUGE_MAX_PATH DELUGE_MAX_FILENAME

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
/// @param error_out       Written with a Deluge `Error` code (as `uint8_t`) on failure.
/// @return The new build, or NULL on failure (nothing adopted, no reason held) with @p error_out set.
DelugeSampleBuild* deluge_audio_load_begin(const char* file_path, const char* alternate_path, uint64_t size_bytes,
                                           uint8_t* error_out);

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
/// @param error_out Written with a Deluge `Error` code (as `uint8_t`) when this returns false and the
///                  status was unresolved (`FILE_UNREADABLE` if the probe finds the dir genuinely
///                  absent, `SD_CARD` if presence couldn't be determined); left untouched otherwise.
/// @return true once the alternate dir is known to exist (already, or newly resolved); false
///         otherwise.
bool deluge_audio_load_resolve_alternate_dir(uint8_t* error_out);

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
uint32_t deluge_audio_load_candidates(const char* file_path, char* out, uint32_t max_candidates);

/// @brief Write the parsed header geometry onto an in-flight build. No I/O.
///
/// Must be given exactly the fields the synchronous `AudioFile::loadFile` writes onto a Sample, or a
/// sample loaded through the async path renders with the wrong geometry -- silently: it loads without
/// error either way. `raw_data_format` matches the C++ `RawDataFormat` enum's values; `midi_note`
/// follows the file's own "unspecified" convention (`midi_note_present == 0` means the file specified
/// no root note, mirroring the -1.0f sentinel `Sample::midiNoteFromFile` uses for the same fact).
///
/// @param build                        An in-flight build from deluge_audio_load_begin.
/// @param num_channels                 Channel count.
/// @param byte_depth                   Bytes per sample word.
/// @param raw_data_format              Matches `RawDataFormat`'s value (never cast a foreign byte
///                                     into the C++ enum type -- match on the number).
/// @param sample_rate                  Sample rate, Hz.
/// @param audio_data_start_pos_bytes   Offset from the start of the file to the first audio byte.
/// @param audio_data_length_bytes      Length of the audio data, in bytes.
/// @param file_loop_start_samples      0 == no loop (the file format's own convention).
/// @param file_loop_end_samples        0 == no loop (the file format's own convention).
/// @param midi_note_present            0 == the file specified no root note; `midi_note_value` is then
///                                     meaningless.
/// @param midi_note_value              The file's declared root note, only meaningful when
///                                     @p midi_note_present is nonzero.
/// @param wavetable_cycle_size         Samples per cycle, carried in case a later Sample→WaveTable
///                                     conversion needs the file's hint.
/// @param wavetable_explicitly_specifies_self  Nonzero if a `clm ` tag declared the file a wavetable.
void deluge_audio_load_apply_geometry(DelugeSampleBuild* build, uint8_t num_channels, uint8_t byte_depth,
                                      uint8_t raw_data_format, uint32_t sample_rate,
                                      uint32_t audio_data_start_pos_bytes, uint32_t audio_data_length_bytes,
                                      uint32_t file_loop_start_samples, uint32_t file_loop_end_samples,
                                      uint8_t midi_note_present, float midi_note_value, uint32_t wavetable_cycle_size,
                                      uint8_t wavetable_explicitly_specifies_self);

/// @brief Apply the parse result, register the fill context, and publish to the cache. No I/O.
///
/// Runs the entire trailer `buildAudioFileFromCard`'s synchronous SAMPLE path used to run after a
/// successful parse -- there is nothing left for the caller to do afterwards, on success or failure.
/// Always consumes @p build: after this call returns, the pointer is invalid regardless of the result.
///
/// @param build    An in-flight build from deluge_audio_load_begin. Consumed either way.
/// @param error_in The parse result (a Deluge `Error` code, as `uint8_t`); `Error::NONE` publishes the
///                 build, anything else destroys it (matching @ref deluge_audio_load_abandon).
/// @return true if the build was published; false if it was destroyed (a non-NONE @p error_in, or a
///         cache-insert failure).
bool deluge_audio_load_finish(DelugeSampleBuild* build, uint8_t error_in);

/// @brief Destroy a build that will never be finished -- a file-open failure, a card pull, or any
///        other path that ends the load before a parse result exists.
///
/// Always safe: releases the protect-during-setup reason and frees the object, so nothing is left
/// adopted-but-abandoned and no reason leaks. Always consumes @p build; the pointer is invalid after
/// this call returns.
///
/// @param build An in-flight build from deluge_audio_load_begin. Consumed.
void deluge_audio_load_abandon(DelugeSampleBuild* build);

/// @brief Queue @p file_path for the async loader and wake it.
///
/// Takes only the path: the loader resolves candidates, opens one, and reads its size itself. Silently
/// drops the request if the queue is full -- the caller has already been told `LOAD_IN_PROGRESS`, and
/// the next cursor move re-requests.
///
/// @param file_path The nominal path to load.
void deluge_audio_load_enqueue(const char* file_path);

/// @brief Whether the Rust async loader backs sample loads on this BSP/config.
/// @return true if async loading is active; false if every load takes the synchronous path.
bool deluge_audio_load_async_active(void);

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_AUDIO_LOAD_H
