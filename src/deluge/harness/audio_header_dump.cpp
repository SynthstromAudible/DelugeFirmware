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

#include "harness/audio_header_dump.h"

#ifdef DELUGE_HOST

#include "storage/audio/audio_byte_source.h"
#include "storage/audio/audio_file_format.h"
#include "util/misc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace {

/// A whole file read via plain `std::fopen`/`fread` into a `std::malloc`'d buffer — deliberately NOT
/// `std::vector`/`operator new`. `DELUGE_HOST` (`memory/operators.cpp`) routes the global `operator
/// new` through `deluge::memory::alloc_external`, the same bounded simulated-SDRAM pool the real
/// device's allocator uses; a multi-megabyte corpus WAV (this differential's corpus runs up to several
/// MB per file) would exhaust it after a handful of files. This dumper is a host-only differential
/// oracle, not a device code path under test, so its own file-read buffer has no business going
/// through that pool at all — plain `malloc`/`free` (which `operators.cpp` does NOT intercept) is
/// exactly the general-purpose host heap this wants.
class FileBuffer {
public:
	explicit FileBuffer(const std::string& path) {
		std::FILE* f = std::fopen(path.c_str(), "rb");
		if (f == nullptr) {
			return;
		}
		if (std::fseek(f, 0, SEEK_END) != 0) {
			std::fclose(f);
			return;
		}
		const long fileSize = std::ftell(f);
		if (fileSize < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
			std::fclose(f);
			return;
		}
		size_ = static_cast<size_t>(fileSize);
		// malloc(0) is valid (may return nullptr or a unique freeable pointer); readOk_ must not
		// depend on a non-null data_ for a genuinely empty file.
		data_ = static_cast<char*>(std::malloc(size_ > 0 ? size_ : 1));
		if (data_ == nullptr) {
			std::fclose(f);
			size_ = 0;
			return;
		}
		const size_t got = size_ > 0 ? std::fread(data_, 1, size_, f) : 0;
		std::fclose(f);
		readOk_ = (got == size_);
	}

	~FileBuffer() { std::free(data_); }

	FileBuffer(const FileBuffer&) = delete;
	FileBuffer& operator=(const FileBuffer&) = delete;

	[[nodiscard]] bool ok() const { return readOk_; }
	[[nodiscard]] const char* data() const { return data_; }
	[[nodiscard]] size_t size() const { return size_; }

private:
	char* data_ = nullptr;
	size_t size_ = 0;
	bool readOk_ = false;
};

/// A forward-reading `AudioByteSource` over a fully-in-memory file (a `FileBuffer`). The whole point of
/// routing this dumper's reads through a raw buffer (rather than reusing `FileByteSource`'s
/// cluster-block machinery) is that this is a differential oracle, not a device code path: it only
/// needs to reproduce what `parseSampleHeader`/`parseWaveTableHeader` compute from a byte source, not
/// how a real embedded reader fetches those bytes.
class InMemoryByteSource final : public AudioByteSource {
public:
	explicit InMemoryByteSource(const FileBuffer& buf) : buf_(buf) {}

	[[nodiscard]] Error read(std::span<std::byte> dest) override {
		if (dest.size() > buf_.size() || pos_ > buf_.size() - dest.size()) {
			return Error::FILE_CORRUPTED;
		}
		std::memcpy(dest.data(), buf_.data() + pos_, dest.size());
		pos_ += dest.size();
		return Error::NONE;
	}

	[[nodiscard]] uint32_t pos() override { return static_cast<uint32_t>(pos_); }

	void seekForwardTo(uint32_t absolutePos) override { pos_ = absolutePos; }

	[[nodiscard]] uint32_t size() const override { return static_cast<uint32_t>(buf_.size()); }

private:
	const FileBuffer& buf_;
	size_t pos_ = 0;
};

// Minimal JSON-string escaping: corpus paths are host filesystem paths, so only '"' and '\\' are
// realistically ever seen, but escape control characters too rather than assume that.
void writeJsonString(std::ostringstream& out, const std::string& s) {
	out << '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			out << "\\\"";
			break;
		case '\\':
			out << "\\\\";
			break;
		case '\n':
			out << "\\n";
			break;
		case '\r':
			out << "\\r";
			break;
		case '\t':
			out << "\\t";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out << buf;
			}
			else {
				out << static_cast<char>(c);
			}
		}
	}
	out << '"';
}

// Independent of parseSampleHeader/parseWaveTableHeader: reads the 12-byte top header's magic bytes
// directly, mirroring detectContainer() in audio_file_format.cpp, so the dumper can report container
// identity even though neither SampleHeader nor WaveTableHeader carries it (it's consumed internally
// and never surfaced — see audio_file_format.h). "unknown" covers both a too-short file and a
// recognised-neither-way one; the Rust side only ever compares this against Ok(AudioHeader), where the
// container is always known, so "unknown" never needs to match anything.
const char* detectContainerTag(const FileBuffer& bytes) {
	if (bytes.size() < 12) {
		return "unknown";
	}
	const char* data = bytes.data();
	if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0) {
		return "wav";
	}
	if (std::memcmp(data, "FORM", 4) == 0 && std::memcmp(data + 8, "AIFF", 4) == 0) {
		return "aiff";
	}
	return "unknown";
}

void writeOptionalFloat(std::ostringstream& out, std::optional<float> v) {
	if (!v) {
		out << "null";
		return;
	}
	// %.9g round-trips an IEEE-754 float32 exactly (9 significant decimal digits is always enough).
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(*v));
	out << buf;
}

void dumpSample(std::ostringstream& out, const FileBuffer& bytes) {
	InMemoryByteSource src(bytes);
	std::expected<SampleHeader, Error> result = parseSampleHeader(src);
	out << "\"sample\":{";
	if (result) {
		const SampleHeader& h = *result;
		out << "\"ok\":true"
		    << ",\"num_channels\":" << static_cast<unsigned>(h.numChannels)
		    << ",\"byte_depth\":" << static_cast<unsigned>(h.byteDepth)
		    << ",\"raw_data_format\":" << static_cast<unsigned>(util::to_underlying(h.rawDataFormat))
		    << ",\"sample_rate\":" << h.sampleRate << ",\"audio_data_start_pos_bytes\":" << h.audioDataStartPosBytes
		    << ",\"audio_data_length_bytes\":" << h.audioDataLengthBytes
		    << ",\"file_loop_start_samples\":" << h.fileLoopStartSamples
		    << ",\"file_loop_end_samples\":" << h.fileLoopEndSamples << ",\"midi_note\":";
		writeOptionalFloat(out, h.midiNote);
		out << ",\"wave_table_cycle_size\":" << h.waveTableCycleSize
		    << ",\"file_explicitly_specifies_self_as_wave_table\":"
		    << (h.fileExplicitlySpecifiesSelfAsWaveTable ? "true" : "false");
	}
	else {
		out << "\"ok\":false,\"error\":" << static_cast<int>(util::to_underlying(result.error()));
	}
	out << "}";
}

void dumpWaveTable(std::ostringstream& out, const char* key, const FileBuffer& bytes, bool insist) {
	InMemoryByteSource src(bytes);
	std::expected<WaveTableHeader, Error> result = parseWaveTableHeader(src, insist);
	out << "\"" << key << "\":{";
	if (result) {
		const WaveTableHeader& h = *result;
		out << "\"ok\":true"
		    << ",\"num_channels\":" << static_cast<unsigned>(h.numChannels)
		    << ",\"byte_depth\":" << static_cast<unsigned>(h.byteDepth)
		    << ",\"raw_data_format\":" << static_cast<unsigned>(util::to_underlying(h.rawDataFormat))
		    << ",\"audio_data_start_pos_bytes\":" << h.audioDataStartPosBytes
		    << ",\"audio_data_length_bytes\":" << h.audioDataLengthBytes << ",\"cycle_size\":" << h.cycleSize;
	}
	else {
		out << "\"ok\":false,\"error\":" << static_cast<int>(util::to_underlying(result.error()));
	}
	out << "}";
}

} // namespace

std::string dumpAudioHeaderAsJson(const std::string& path) {
	std::ostringstream out;
	out << "{\"path\":";
	writeJsonString(out, path);

	FileBuffer bytes(path);
	if (!bytes.ok()) {
		out << ",\"read_error\":true}";
		return out.str();
	}

	out << ",\"read_error\":false,\"container\":\"" << detectContainerTag(bytes) << "\",";
	dumpSample(out, bytes);
	out << ",";
	dumpWaveTable(out, "wavetable_insist_false", bytes, /*insist=*/false);
	out << ",";
	dumpWaveTable(out, "wavetable_insist_true", bytes, /*insist=*/true);
	out << "}";
	return out.str();
}

#endif // DELUGE_HOST
