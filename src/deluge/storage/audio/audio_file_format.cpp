/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "storage/audio/audio_file_format.h"
#include "libdeluge/audio_format.h" // the Rust WAV/AIFF decoder's C ABI
#include "storage/audio/audio_byte_source.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>

// The chunk walk itself lives in Rust (`crates/deluge_audio_format`), which is fuzzed and held against
// the whole sample corpus by a differential test. What is left here is the adapter: it turns
// AudioByteSource's pull-with-seek surface into the window requests the decoder asks for, and lowers the
// decoder's plain header value onto SampleHeader / WaveTableHeader.
//
// The decoder never asks for the audio payload — it steps over it by arithmetic — so this reads a few
// dozen bytes per file no matter how big the file is.

namespace {

/// How much to hand back when the decoder asks for a window, for the Sample walk.
///
/// DELUGE_AUDIO_MAX_WINDOW_BYTES (52) is the FLOOR, not the serving size: the decoder satisfies as many
/// further requests as one window covers, so serving exact windows costs one round trip per chunk header.
/// That is fine for a healthy file and pathological for a common kind of damage — a `data` chunk that
/// under-declares its length (a recorder that never fixed up its header, a card that filled mid-write)
/// makes the walk read the audio itself as a stream of nonsense chunk headers: 131,075 feeds on a 1 MB
/// file at exact windows, 2,050 at this size.
///
/// Not larger, though, because a read that crosses a cluster boundary costs a real SD read of a whole
/// cluster: at 512 bytes only ~1.5% of refills can pull in a cluster the walk did not need.
constexpr uint32_t kSampleReadAheadBytes = 512;

/// The WaveTable walk deliberately reads NOTHING it was not asked for — see parseWaveTableHeader.
constexpr uint32_t kNoReadAhead = 0;

/// Feeds the decoder's window requests off an AudioByteSource.
///
/// It keeps one buffer of recently-read bytes, which is what makes a forward-only byte source enough: the
/// decoder's requests never move backwards, but they can land *inside* bytes already read (a chunk that
/// declares less payload than the arm reading it looks at, e.g. a `clm ` chunk). Those are served from
/// the buffer instead of by a seek the source cannot do.
class WindowFeeder {
public:
	/// @param src       Positioned at the start of the file; every offset the decoder names is absolute.
	/// @param readAhead Extra bytes to pull on a refill, beyond the request. 0 reads exactly what is asked.
	WindowFeeder(AudioByteSource& src, uint32_t readAhead)
	    : src_(src), readAhead_(readAhead), bufferStart_(src.pos()) {}

	/// Make the @p len bytes at absolute file offset @p start available.
	///
	/// @return Everything the buffer holds from @p start onwards — which is deliberately allowed to be
	///         MORE than @p len, and is what the decoder wants — or the source's read error.
	[[nodiscard]] std::expected<std::span<const std::byte>, Error> window(uint32_t start, uint32_t len) {
		const uint32_t held = bufferStart_ + bufferLength_; // == src_.pos(), maintained below.

		// A zero-length request is a real request (an empty file, or a walk that has reached the end):
		// it must still be fed, but there is nothing to read for it.
		if (len == 0) {
			return (start >= bufferStart_ && start <= held) ? available(start) : std::span<const std::byte>{};
		}
		if (len > buffer_.size()) {
			// Unreachable: every request is at most DELUGE_AUDIO_MAX_WINDOW_BYTES, which the buffer is
			// sized above. Reported rather than assumed so that a decoder that grew its windows past the
			// buffer would fail the file rather than overrun.
			return std::unexpected(Error::BUG);
		}
		if (start >= bufferStart_ && start + len <= held) {
			return available(start); // Already in hand.
		}
		if (start < bufferStart_) {
			// Unreachable: the walk only ever moves forward, which is what lets a forward-only source
			// serve it at all. Fail the file rather than silently read the wrong bytes.
			return std::unexpected(Error::BUG);
		}

		// Keep the part of the buffer that lies ahead of `start` — the source cannot re-read it — and
		// top it up. Beyond the buffer, seek: the payload between here and there is never read.
		uint32_t keep = 0;
		if (start < held) {
			keep = held - start;
			std::memmove(buffer_.data(), buffer_.data() + (start - bufferStart_), keep);
		}
		else if (start > held) {
			src_.seekForwardTo(start);
		}
		bufferStart_ = start;
		bufferLength_ = keep;

		// Requests are clamped to the file by the decoder, so `remaining >= len`; clamping again here is
		// what keeps the read-ahead from asking for bytes past the end (which the source rejects
		// outright, reading nothing).
		const uint32_t remaining = src_.size() - start;
		const uint32_t want = std::min({std::max(len, readAhead_), static_cast<uint32_t>(buffer_.size()), remaining});
		if (want > keep) {
			if (const Error error = src_.read({buffer_.data() + keep, want - keep}); error != Error::NONE) {
				return std::unexpected(error);
			}
			bufferLength_ = want;
		}
		return available(start);
	}

private:
	/// The buffer from @p start onwards. @p start must lie within the buffer.
	[[nodiscard]] std::span<const std::byte> available(uint32_t start) const {
		const uint32_t offset = start - bufferStart_;
		return std::span<const std::byte>{buffer_.data() + offset, bufferLength_ - offset};
	}

	AudioByteSource& src_;
	uint32_t readAhead_;
	uint32_t bufferStart_;      ///< Absolute file offset of buffer_[0].
	uint32_t bufferLength_ = 0; ///< Valid bytes in buffer_; bufferStart_ + this == src_.pos().
	std::array<std::byte, kSampleReadAheadBytes> buffer_{};
};

static_assert(kSampleReadAheadBytes >= DELUGE_AUDIO_MAX_WINDOW_BYTES,
              "the buffer must be able to hold the largest window the decoder can ask for");

/// The decoder's terminal status as one of the two error values this file has ever reported.
Error decodeError(uint8_t status) {
	switch (status) {
	case DELUGE_AUDIO_PARSE_NOT_AUDIO:   // Neither RIFF/WAVE nor FORM/AIFF.
	case DELUGE_AUDIO_PARSE_UNSUPPORTED: // Bit depth, format tag, channel count or sample rate.
		return Error::FILE_UNSUPPORTED;
	case DELUGE_AUDIO_PARSE_MALFORMED: // No fmt/COMM or no data/SSND chunk.
	case DELUGE_AUDIO_PARSE_TRUNCATED: // The file ends inside its own header.
		return Error::FILE_CORRUPTED;
	default:
		return Error::BUG; // A null pointer or undersized storage: our fault, not the file's.
	}
}

/// How far the walk should go, and what it may give up on early. The Sample and WaveTable parses differ
/// only in these two answers.
struct WalkOptions {
	/// Stop as soon as the audio data chunk has been located, rather than walking the whole header. See
	/// parseWaveTableHeader for why that is not just an optimisation.
	bool stopAtAudioData = false;
	/// Give up with FILE_NOT_LOADABLE_AS_WAVETABLE the moment the container turns out to be AIFF, before
	/// reading a chunk — again see parseWaveTableHeader.
	bool rejectAiff = false;
};

/// Drive the decoder over @p src.
std::expected<DelugeAudioHeader, Error> decodeHeader(AudioByteSource& src, WalkOptions options) {
	DelugeAudioHeaderParser parser;
	if (!deluge_audio_header_parser_init(&parser, sizeof(parser), src.size())) {
		return std::unexpected(Error::BUG); // Storage too small — a build-time mismatch, not a file.
	}

	WindowFeeder feeder{src, options.stopAtAudioData ? kNoReadAhead : kSampleReadAheadBytes};
	DelugeAudioHeader header{};

	while (true) {
		// The container is known after the first window, which is where the AIFF verdict was made before
		// this file called into Rust — and it has to stay there: it is a verdict about the container, not
		// about whether the file decodes.
		uint8_t container = 0;
		if (options.rejectAiff && deluge_audio_header_parser_container(&parser, &container)
		    && container == DELUGE_AUDIO_CONTAINER_AIFF) {
			return std::unexpected(Error::FILE_NOT_LOADABLE_AS_WAVETABLE);
		}

		if (options.stopAtAudioData && deluge_audio_header_parser_located_audio_data(&parser)) {
			const uint8_t status = deluge_audio_header_parser_finish_here(&parser, &header);
			if (status != DELUGE_AUDIO_PARSE_DONE) {
				return std::unexpected(decodeError(status));
			}
			return header;
		}

		DelugeAudioWindow want{};
		if (!deluge_audio_header_parser_pending(&parser, &want)) {
			// Unreachable: every terminal transition happens inside feed, which reports it below.
			return std::unexpected(Error::BUG);
		}

		// `want.len` can legitimately be zero, and the empty window must still be fed: the decoder only
		// advances inside feed, so skipping the call because there is nothing to read would spin here
		// forever.
		const std::expected<std::span<const std::byte>, Error> bytes = feeder.window(want.start, want.len);
		if (!bytes) {
			return std::unexpected(bytes.error());
		}

		const uint8_t status =
		    deluge_audio_header_parser_feed(&parser, reinterpret_cast<const uint8_t*>(bytes->data()),
		                                    static_cast<uint32_t>(bytes->size()), want.start, &header);
		if (status == DELUGE_AUDIO_PARSE_DONE) {
			return header;
		}
		if (status != DELUGE_AUDIO_PARSE_NEED) {
			return std::unexpected(decodeError(status));
		}
	}
}

/// The wavetable acceptance verdict as the Error values callers switch on.
Error wavetableError(uint8_t verdict) {
	switch (verdict) {
	case DELUGE_WAVETABLE_REJECT_STEREO:
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO;
	case DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE:
	// An AIFF container is rejected as "not a wavetable" rather than "unsupported", matching the check
	// this replaces (which ran before the walk and returned exactly this).
	case DELUGE_WAVETABLE_REJECT_AIFF:
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
	case DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH:
		// Unreachable through a successful parse (which always saw a fmt/COMM chunk).
		return Error::FILE_UNSUPPORTED;
	default:
		return Error::BUG; // A null pointer: our fault, not the file's.
	}
}

} // namespace

std::expected<SampleHeader, Error> parseSampleHeader(AudioByteSource& src) {
	const std::expected<DelugeAudioHeader, Error> header = decodeHeader(src, WalkOptions{});
	if (!header) {
		return std::unexpected(header.error());
	}
	return SampleHeader{
	    .numChannels = header->num_channels,
	    .byteDepth = header->byte_depth,
	    .rawDataFormat = static_cast<RawDataFormat>(header->raw_data_format),
	    .sampleRate = header->sample_rate,
	    .audioDataStartPosBytes = header->audio_data_start_pos_bytes,
	    .audioDataLengthBytes = header->audio_data_length_bytes,
	    .fileLoopStartSamples = header->file_loop_start_samples,
	    .fileLoopEndSamples = header->file_loop_end_samples,
	    .midiNote = header->midi_note.has != 0 ? std::optional<float>{header->midi_note.value} : std::nullopt,
	    .waveTableCycleSize = header->wavetable_cycle_size,
	    .fileExplicitlySpecifiesSelfAsWaveTable = header->wavetable_explicitly_specifies_self != 0,
	};
}

std::expected<WaveTableHeader, Error> parseWaveTableHeader(AudioByteSource& src, bool makeWaveTableWorkAtAllCosts) {
	// Two ways this walk is deliberately shorter than the Sample one, both of them behaviour the parser
	// this replaces had:
	//
	//  - It stops at the audio data chunk. Not an optimisation: the wavetable load hands this SAME byte
	//    source to WaveTable::setup, which seeks back to audioDataStartPosBytes and reads the audio
	//    through the source's own block buffer. Walking on past the audio (to pick up metadata a
	//    wavetable does not use) would leave a sequentially-read source blocks ahead of the audio and
	//    make that seek a backwards one.
	//  - It gives up on AIFF as soon as the container is known, unless the user insists. That has to
	//    happen before the walk, not after: sample_browser.cpp reads FILE_NOT_LOADABLE_AS_WAVETABLE as
	//    "not a wavetable, load it as a Sample instead" and any other error as "give up and show it", so
	//    an AIFF whose wavetable decode failed for some other reason would stop being offered as a
	//    Sample at all.
	const std::expected<DelugeAudioHeader, Error> header =
	    decodeHeader(src, WalkOptions{.stopAtAudioData = true, .rejectAiff = !makeWaveTableWorkAtAllCosts});
	if (!header) {
		return std::unexpected(header.error());
	}

	uint32_t cycleSize = 0;
	const uint8_t verdict = deluge_audio_header_accept_as_wavetable(&*header, makeWaveTableWorkAtAllCosts, &cycleSize);
	if (verdict != DELUGE_WAVETABLE_ACCEPTED) {
		return std::unexpected(wavetableError(verdict));
	}

	return WaveTableHeader{
	    .numChannels = header->num_channels, // Validated == 1 by the acceptance check above.
	    .byteDepth = header->byte_depth,
	    .rawDataFormat = static_cast<RawDataFormat>(header->raw_data_format),
	    .audioDataStartPosBytes = header->audio_data_start_pos_bytes,
	    .audioDataLengthBytes = header->audio_data_length_bytes,
	    .cycleSize = cycleSize,
	};
}
