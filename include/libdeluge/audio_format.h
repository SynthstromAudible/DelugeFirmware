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

#ifndef LIBDELUGE_AUDIO_FORMAT_H
#define LIBDELUGE_AUDIO_FORMAT_H
#include <stdbool.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdalign.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

/// @file
/// @brief WAV/AIFF header decoding, implemented in Rust (`crates/deluge_audio_format`).
///
/// The parse is a RESUMABLE STATE MACHINE driven from bounded windows, not a one-shot call over a
/// buffer: init it with the file's size, ask what it wants, read exactly that, feed it, repeat. The
/// caller owns every read, so the same decoder serves a blocking fiber, an async task, or a test
/// harness over an in-memory buffer.
///
/// It is windowed because it has to be. A prefix-only protocol ("give me the first N bytes") walks
/// the caller through the audio payload to reach the metadata behind it — measured over the sample
/// corpus, the required prefix was 100% of the file at the median, p90 AND maximum, up to 183 MB for
/// one file, on a device with 8 MB of SDRAM. Here the payload is stepped over by arithmetic and never
/// read at all: peak memory is one window.
///
/// Two rules a caller must not get wrong. Both stall the walk forever:
///
///  - **A zero-length request is a real request.** `deluge_audio_header_parser_pending` can report
///    `len == 0` (an empty file, or a walk that has reached the end). The parser only ever advances
///    inside `feed`, so "nothing to read, skip the feed" spins on the same request until the end of
///    time. Feed the empty window.
///  - **`DELUGE_AUDIO_PARSE_TRUNCATED` is not "supply more".** The parser is told the file's size up
///    front and never asks for a byte past it, so truncation is a fact about the file, not a request.
///    There is no retry loop to write.

/// @brief The largest window the parser can ever ask for, in bytes.
///
/// A FLOOR for the caller's buffer, not a serving size. Handing back MORE than was asked for — a
/// whole SD block, ideally — is wanted, not merely tolerated: the parser satisfies as many further
/// requests from the same window as it fully covers, and the difference is 131,075 feeds at exact
/// windows versus 2,050 at a 512-byte (one SD block) serving size, on a 1 MB file whose `data` chunk
/// under-declares its length (a recorder that never fixed up its header; a card that filled
/// mid-write).
#define DELUGE_AUDIO_MAX_WINDOW_BYTES 52u

/// @brief `sizeof(DelugeAudioHeaderParser)` in 64-bit words.
///
/// Must stay >= the Rust `HeaderParser`'s own size; `deluge_audio_header_parser_init` is handed this
/// struct's `sizeof` and refuses storage too small for it, so the two sides cannot silently drift.
#define DELUGE_AUDIO_HEADER_PARSER_WORDS 24u

/// @brief Which container a file uses. Values match the Rust `Container` discriminants.
typedef enum DelugeAudioContainer : uint8_t {
	DELUGE_AUDIO_CONTAINER_WAV = 0,
	DELUGE_AUDIO_CONTAINER_AIFF = 1,
} DelugeAudioContainer;

/// @brief What a walk is FOR: which chunks it reads, and which of the file's claims it insists on.
///
/// Not a style preference. The parser this replaces branched on the same distinction, and dropping it
/// changed which files load: a wavetable load uses only the audio's shape and location, so the
/// metadata that describes playing a NOTE back is neither read nor believed.
typedef enum DelugeAudioWalkGoal : uint8_t {
	/// Everything the header says. What a Sample load wants.
	DELUGE_AUDIO_WALK_WHOLE_HEADER = 0,
	/// Only what locates and shapes the audio. What a WaveTable load wants:
	///
	///  - `smpl`/`inst`/`INST` are stepped over UNREAD. Those chunks are read at a fixed extent rather
	///    than at their declared length, so reading a `smpl` that declares less than 52 bytes (a
	///    no-loops sampler chunk declares 36) walks the CALLER's byte source past the audio chunk
	///    header behind it — and a wavetable load hands that same source, position and all, to
	///    WaveTable::setup.
	///  - The sample rate is still reported but no longer has to be one we can play. A 192 kHz
	///    single-cycle file is a perfectly good wavetable; wavetable rendering never reads the rate.
	DELUGE_AUDIO_WALK_AUDIO_ONLY = 1,
} DelugeAudioWalkGoal;

/// @brief Outcome of one `feed` / `finish_here` call.
///
/// @note Fixed underlying type (`: uint8_t`): these cross the FFI by value, and `arm-none-eabi`
///       defaults to SHORT ENUMS, so the width must be pinned by hand on both sides rather than left
///       to whatever the C++ build's default enum happens to be.
typedef enum DelugeAudioParseStatus : uint8_t {
	/// The parser wants the window `deluge_audio_header_parser_pending` reports.
	DELUGE_AUDIO_PARSE_NEED = 0,
	/// Decoded; the header has been written to the caller's out-parameter.
	DELUGE_AUDIO_PARSE_DONE = 1,
	/// No container we recognise — not an audio file at all.
	DELUGE_AUDIO_PARSE_NOT_AUDIO = 2,
	/// The file contradicts itself (no `fmt `/`COMM`, or no `data`/`SSND`).
	DELUGE_AUDIO_PARSE_MALFORMED = 3,
	/// Valid, but says something we cannot play (bit depth, format tag, channel count, sample rate).
	DELUGE_AUDIO_PARSE_UNSUPPORTED = 4,
	/// The file ends before its header does. NOT a request for more bytes.
	DELUGE_AUDIO_PARSE_TRUNCATED = 5,
	/// The call was malformed — a null pointer, or storage too small. A programming error on this
	/// side of the boundary, never a property of a file.
	DELUGE_AUDIO_PARSE_BAD_CALL = 6,
} DelugeAudioParseStatus;

/// @brief Verdict of `deluge_audio_header_accept_as_wavetable`.
typedef enum DelugeWavetableAccept : uint8_t {
	/// Usable; the cycle size to render with has been written out.
	DELUGE_WAVETABLE_ACCEPTED = 0,
	/// More than one channel. Rejected even when the caller insists.
	DELUGE_WAVETABLE_REJECT_STEREO = 1,
	/// Bit depth unknown, so the length-in-samples check cannot be made.
	DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH = 2,
	/// No `clm ` tag and a length that isn't a whole number of 2048-sample cycles.
	DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE = 3,
	/// An AIFF container, which is only ever a wavetable if the caller insists.
	DELUGE_WAVETABLE_REJECT_AIFF = 4,
	/// A null pointer was passed. A programming error, not a file's fault.
	DELUGE_WAVETABLE_BAD_CALL = 5,
} DelugeWavetableAccept;

/// @brief An optional float, as a flag plus a value rather than a NaN sentinel.
///
/// "The file said nothing" is a fact about the file; a sentinel would make every consumer responsible
/// for remembering which bit pattern encodes it.
typedef struct DelugeOptionalF32 {
	uint8_t has; ///< 0 == the file specified no value; `value` is then meaningless.
	float value;
} DelugeOptionalF32;

/// @brief Everything a WAV/AIFF header says. A plain value: no behaviour, no ownership.
typedef struct DelugeAudioHeader {
	uint8_t container; ///< DelugeAudioContainer
	uint8_t num_channels;
	uint8_t byte_depth;
	uint8_t raw_data_format; ///< Matches the C++ `RawDataFormat` value for value.
	uint32_t sample_rate;
	uint32_t audio_data_start_pos_bytes; ///< Offset from the start of the file to the first audio byte.
	uint32_t audio_data_length_bytes;
	/// 0 == no loop. The WAV/AIFF format's own convention (a degenerate 0,0 range), not a parser
	/// sentinel, and the two can be set independently.
	uint32_t file_loop_start_samples;
	uint32_t file_loop_end_samples;
	DelugeOptionalF32 midi_note;
	uint32_t wavetable_cycle_size;
	uint8_t wavetable_explicitly_specifies_self; ///< 0/1: a `clm ` tag declared the file a wavetable.
} DelugeAudioHeader;

/// @brief One window request: `len` bytes at absolute file offset `start`.
///
/// Both halves are inside the file. `len` CAN legitimately be 0 — see the file comment.
typedef struct DelugeAudioWindow {
	uint32_t start;
	uint32_t len;
} DelugeAudioWindow;

/// @brief Caller-owned storage for one in-flight parse.
///
/// Opaque: the parser's layout stays Rust-side and is deliberately not mirrored here. The decoder has
/// no allocator, so the memory comes from the caller — a local is the intended home (this is well
/// under 256 bytes, against a fiber stack measured in tens of KiB).
typedef struct DelugeAudioHeaderParser {
	/// `uint64_t` elements AND an explicit `alignas(8)`: on 32-bit x86 (the -m32 spec/sim builds)
	/// `alignof(uint64_t)` is 4, which would under-align the Rust value living here.
	alignas(8) uint64_t opaque[DELUGE_AUDIO_HEADER_PARSER_WORDS];
} DelugeAudioHeaderParser;

/// @brief Place a fresh parser for a @p file_size -byte file in @p parser.
///
/// @param parser        Caller-owned storage.
/// @param storage_bytes The caller's own `sizeof(DelugeAudioHeaderParser)`.
/// @param file_size     The FILESYSTEM's size for the file, passed straight through. It is what bounds
///                      the walk — no request can name a byte past it, so the end of the file is not
///                      something the caller has to signal, and cannot get wrong. Never pass a computed
///                      or inferred value: a lying size bounds the parse to what it claimed.
/// @param goal          A DelugeAudioWalkGoal. An unrecognised value is REFUSED, not defaulted:
///                      falling back to DELUGE_AUDIO_WALK_WHOLE_HEADER is the safe direction for
///                      which fields come back and the wrong one for the caller's byte source, since a
///                      wavetable caller that mistyped the byte would silently get the over-reading
///                      walk back.
/// @return false (having written nothing) if @p parser is null, @p storage_bytes is too small for the
///         parser, or @p goal is not a DelugeAudioWalkGoal; nothing else can fail.
bool deluge_audio_header_parser_init(DelugeAudioHeaderParser* parser, uint32_t storage_bytes, uint32_t file_size,
                                     uint8_t goal);

/// @brief The window the parser is waiting for.
///
/// @param parser An initialised parser.
/// @param out    Receives the request. `out->len` may be 0; feed it an empty window rather than
///               skipping the feed, or the walk never terminates.
/// @return false once the walk is over (done or failed), leaving @p out untouched.
bool deluge_audio_header_parser_pending(const DelugeAudioHeaderParser* parser, DelugeAudioWindow* out);

/// @brief Hand the parser bytes read from the file.
///
/// Supplying MORE than was asked for is wanted (see DELUGE_AUDIO_MAX_WINDOW_BYTES).
///
/// @warning Supplying LESS than was asked for is how a caller reports a short read — there is
///          nothing to pad and no reason to. The request was already clamped to the file, so the
///          bytes exist; @p window_len must be the count actually obtained, or
///          the parser cannot tell a real short read from a full one and will walk forward on
///          fabricated data instead of concluding the file is truncated. Report the true, shorter
///          length and the walk ends cleanly with DELUGE_AUDIO_PARSE_TRUNCATED rather than
///          continuing on a half-built header.
///
/// @param parser       An initialised parser.
/// @param window       The bytes, or null when @p window_len is 0.
/// @param window_len   How many bytes @p window holds.
/// @param window_start The absolute file offset @p window begins at.
/// @param header_out   Written only on DELUGE_AUDIO_PARSE_DONE.
/// @return A DelugeAudioParseStatus. DELUGE_AUDIO_PARSE_NEED means "ask `pending` again"; every other
///         value is terminal.
uint8_t deluge_audio_header_parser_feed(DelugeAudioHeaderParser* parser, const uint8_t* window, uint32_t window_len,
                                        uint32_t window_start, DelugeAudioHeader* header_out);

/// @brief Whether the walk has located the audio data.
///
/// True once a `data`/`SSND` chunk has been read and the header's audio start/length can no longer
/// change. Pair it with deluge_audio_header_parser_finish_here() to stop the walk there — which is what
/// a WaveTable load wants: everything past the audio chunk is metadata a wavetable does not use, and
/// walking it would drag the caller's byte source past the audio it is about to read.
///
/// @param parser An initialised parser.
/// @return true once the audio chunk has been located.
bool deluge_audio_header_parser_located_audio_data(const DelugeAudioHeaderParser* parser);

/// @brief The container the file declares, once the 12-byte top header has been read.
///
/// Known after the very first feed, long before the header is decoded. It is exposed for the one
/// verdict that depends on the container alone and should not wait for the rest of the walk: a
/// wavetable load rejects AIFF outright unless the caller insists, and reporting a DECODE failure
/// instead for a malformed AIFF changes what the caller does with it (`sample_browser.cpp` reads
/// DELUGE_WAVETABLE_REJECT_AIFF's error as "not a wavetable, try it as a sample", and any other
/// error as "give up").
///
/// @param parser An initialised parser.
/// @param out    Receives a DelugeAudioContainer.
/// @return false, leaving @p out untouched, before the top header has been read and on a walk that
///         failed without identifying a container.
bool deluge_audio_header_parser_container(const DelugeAudioHeaderParser* parser, uint8_t* out);

/// @brief End the walk here and resolve the header from what has been read so far.
///
/// The same finalisation the end of the file would have run, just earlier: fields the remaining chunks
/// would have set keep their "the file said nothing" defaults. Fails for the same reasons the end of a
/// file does (no `data`/`SSND`, no `fmt `/`COMM`), so it is only meaningful once
/// deluge_audio_header_parser_located_audio_data() is true.
///
/// @param parser     An initialised parser.
/// @param header_out Written only on DELUGE_AUDIO_PARSE_DONE.
/// @return A DelugeAudioParseStatus; never DELUGE_AUDIO_PARSE_NEED — this call always terminates.
uint8_t deluge_audio_header_parser_finish_here(DelugeAudioHeaderParser* parser, DelugeAudioHeader* header_out);

/// @brief Decide whether a decoded header may be used as a wavetable.
///
/// @param header         A header a parse produced.
/// @param insist         Mirrors `makeWaveTableWorkAtAllCosts`: waives the `clm `/length-shape check and
///                       the AIFF rejection, but never the mono requirement.
/// @param cycle_size_out Written only on DELUGE_WAVETABLE_ACCEPTED.
/// @return A DelugeWavetableAccept.
uint8_t deluge_audio_header_accept_as_wavetable(const DelugeAudioHeader* header, bool insist, uint32_t* cycle_size_out);

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_AUDIO_FORMAT_H
