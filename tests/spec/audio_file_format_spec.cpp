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

// Descriptor regression net for the WAV/AIFF header parser (parseAudioFileHeader). Feeds hand-crafted byte
// buffers through an in-memory AudioByteSource and asserts each parsed AudioFileFormat field against a
// value derived from first principles (the WAV/AIFF spec) — independent of the parser implementation, so it
// holds across the file-loading redesign. The expected values encode the *current, correct* behavior.

#include "storage/audio/audio_byte_source.h"
#include "storage/audio/audio_file_format.h"
#include "util/misc.h"
#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

// In-memory byte source mirroring the legacy reader contract: a forward cursor, a strict past-EOF check
// (FILE_CORRUPTED), and an absolute forward seek. `startPos` models the 12-byte RIFF/FORM top header that
// AudioFileManager::getAudioFileFromFilename consumes before invoking the parser.
class MemoryByteSource final : public AudioByteSource {
public:
	explicit MemoryByteSource(std::span<const std::byte> data, uint32_t startPos) : data_(data), pos_(startPos) {}

	Error read(std::span<std::byte> dest) override {
		if (static_cast<uint64_t>(pos_) + dest.size() > data_.size()) {
			return Error::FILE_CORRUPTED;
		}
		std::copy_n(data_.begin() + pos_, dest.size(), dest.begin());
		pos_ += static_cast<uint32_t>(dest.size());
		return Error::NONE;
	}
	uint32_t pos() override { return pos_; }
	void seekForwardTo(uint32_t absolutePos) override { pos_ = absolutePos; }
	uint32_t size() const override { return static_cast<uint32_t>(data_.size()); }

private:
	std::span<const std::byte> data_;
	uint32_t pos_;
};

// Little-/big-endian byte assembler.
struct ByteWriter {
	std::vector<std::byte> bytes;
	void u8(uint32_t v) { bytes.push_back(static_cast<std::byte>(v & 0xFF)); }
	void tag(const char* t) {
		for (int i = 0; i < 4; i++) {
			u8(static_cast<uint8_t>(t[i]));
		}
	}
	void le16(uint16_t v) {
		u8(v);
		u8(v >> 8);
	}
	void le32(uint32_t v) {
		u8(v);
		u8(v >> 8);
		u8(v >> 16);
		u8(v >> 24);
	}
	void be16(uint16_t v) {
		u8(v >> 8);
		u8(v);
	}
	void be32(uint32_t v) {
		u8(v >> 24);
		u8(v >> 16);
		u8(v >> 8);
		u8(v);
	}
	void raw(std::initializer_list<uint8_t> vs) {
		for (uint8_t v : vs) {
			u8(v);
		}
	}
	void zeros(uint32_t n) {
		for (uint32_t i = 0; i < n; i++) {
			u8(0);
		}
	}
	void append(const ByteWriter& o) { bytes.insert(bytes.end(), o.bytes.begin(), o.bytes.end()); }
};

// Wrap a RIFF/WAVE container around an already-assembled chunk body.
std::vector<std::byte> wrapWav(const ByteWriter& body) {
	ByteWriter w;
	w.tag("RIFF");
	w.le32(static_cast<uint32_t>(4 + body.bytes.size()));
	w.tag("WAVE");
	w.append(body);
	return w.bytes;
}

// A 16-byte WAV "fmt " chunk.
ByteWriter wavFmt(uint16_t bits, uint16_t channels, uint32_t sampleRate, uint16_t format = 1 /*PCM*/) {
	ByteWriter b;
	b.tag("fmt ");
	b.le32(16);
	uint16_t blockAlign = channels * (bits / 8);
	b.le16(format);
	b.le16(channels);
	b.le32(sampleRate);
	b.le32(sampleRate * blockAlign); // byte rate
	b.le16(blockAlign);
	b.le16(bits);
	return b;
}

// A "data" chunk of `len` zero bytes.
ByteWriter wavData(uint32_t len) {
	ByteWriter b;
	b.tag("data");
	b.le32(len);
	b.zeros(len);
	return b;
}

constexpr uint32_t kTopHeader = 12; // RIFF/size/WAVE consumed before the parser runs
constexpr int rawFmt(RawDataFormat f) {
	return static_cast<int>(util::to_underlying(f));
}

AudioFileFormat parse(const std::vector<std::byte>& file, AudioFileType type, bool isAiff, Error& err,
                      bool atAllCosts = false) {
	MemoryByteSource src{file, kTopHeader};
	AudioFileFormat out{};
	err = parseAudioFileHeader(src, type, atAllCosts, isAiff, out);
	return out;
}

} // namespace

// clang-format off
describe audio_file_format("parseAudioFileHeader", $ {
	context("WAV samples", _ {
		it("decodes 16-bit stereo PCM", _ {
			ByteWriter body; body.append(wavFmt(16, 2, 44100)); body.append(wavData(100));
			Error err; AudioFileFormat f = parse(wrapWav(body), AudioFileType::SAMPLE, false, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(static_cast<int>(f.numChannels)).to_equal(2);
			expect(static_cast<int>(f.byteDepth)).to_equal(2);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::NATIVE));
			expect((int)f.sampleRate).to_equal(44100);
			expect((int)f.audioDataStartPosBytes).to_equal(44); // 12 top +8 fmt hdr +16 fmt +8 data hdr
			expect((int)f.audioDataLengthBytes).to_equal(100);
			expect(f.midiNoteFromFile).to_equal(-1.0f);
			expect((int)f.fileLoopStartSamples).to_equal(0);
			expect((int)f.waveTableCycleSize).to_equal(2048);
			expect(f.fileExplicitlySpecifiesSelfAsWaveTable).to_equal(false);
		});

		it("decodes 8-bit mono as UNSIGNED_8", _ {
			ByteWriter body; body.append(wavFmt(8, 1, 22050)); body.append(wavData(50));
			Error err; AudioFileFormat f = parse(wrapWav(body), AudioFileType::SAMPLE, false, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(static_cast<int>(f.numChannels)).to_equal(1);
			expect(static_cast<int>(f.byteDepth)).to_equal(1);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::UNSIGNED_8));
			expect((int)f.sampleRate).to_equal(22050);
		});

		it("decodes 24-bit mono PCM", _ {
			ByteWriter body; body.append(wavFmt(24, 1, 48000)); body.append(wavData(30));
			Error err; AudioFileFormat f = parse(wrapWav(body), AudioFileType::SAMPLE, false, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(static_cast<int>(f.byteDepth)).to_equal(3);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::NATIVE));
		});

		it("decodes 32-bit float as FLOAT", _ {
			ByteWriter body; body.append(wavFmt(32, 1, 44100, 3 /*IEEE float*/)); body.append(wavData(40));
			Error err; AudioFileFormat f = parse(wrapWav(body), AudioFileType::SAMPLE, false, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(static_cast<int>(f.byteDepth)).to_equal(4);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::FLOAT));
		});

		it("reads root note + loop points from a smpl chunk", _ {
			ByteWriter smpl; smpl.tag("smpl"); smpl.le32(60);
			smpl.le32(0); smpl.le32(0); smpl.le32(0);      // manuf, product, samplePeriod
			smpl.le32(60);                                  // [3] MIDI unity note
			smpl.le32(0);                                   // [4] pitch fraction
			smpl.le32(0); smpl.le32(0);                     // [5] smpteFormat, [6] smpteOffset
			smpl.le32(1);                                   // [7] num sample loops
			smpl.le32(0);                                   // [8] sampler data
			smpl.le32(0); smpl.le32(0);                     // loop: id, type
			smpl.le32(1000);                                // [2] loop start
			smpl.le32(2000);                                // [3] loop end
			smpl.le32(0); smpl.le32(0);                     // fraction, playCount
			ByteWriter body; body.append(wavFmt(16, 1, 44100)); body.append(smpl); body.append(wavData(8));
			Error err; AudioFileFormat f = parse(wrapWav(body), AudioFileType::SAMPLE, false, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(f.midiNoteFromFile).to_equal(60.0f);
			expect((int)f.fileLoopStartSamples).to_equal(1000);
			expect((int)f.fileLoopEndSamples).to_equal(2000);
		});

		it("returns FILE_CORRUPTED on a truncated fmt chunk", _ {
			ByteWriter body; body.tag("fmt "); body.le32(16); body.le32(0); // claims 16, supplies 4
			Error err; parse(wrapWav(body), AudioFileType::SAMPLE, false, err);
			expect(static_cast<int>(err)).to_equal(static_cast<int>(Error::FILE_CORRUPTED));
		});
	});

	context("WAV wavetables", _ {
		it("accepts a clm-tagged single-cycle file and reports the cycle size", _ {
			ByteWriter clm; clm.tag("clm "); clm.le32(7); clm.raw({'<','!','>','2','0','4','8'});
			clm.u8(0); // RIFF pad byte: odd (7-byte) chunk is padded to an even boundary
			ByteWriter body; body.append(wavFmt(16, 1, 44100)); body.append(clm); body.append(wavData(4096));
			Error err; AudioFileFormat f = parse(wrapWav(body), AudioFileType::WAVETABLE, false, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(f.fileExplicitlySpecifiesSelfAsWaveTable).to_equal(true);
			expect((int)f.waveTableCycleSize).to_equal(2048);
			expect((int)f.audioDataLengthBytes).to_equal(4096);
		});

		it("rejects a stereo file as a wavetable", _ {
			ByteWriter body; body.append(wavFmt(16, 2, 44100)); body.append(wavData(4096));
			Error err; parse(wrapWav(body), AudioFileType::WAVETABLE, false, err);
			expect(static_cast<int>(err))
			    .to_equal(static_cast<int>(Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO));
		});
	});

	context("AIFF samples", _ {
		it("decodes a 16-bit mono COMM/SSND file (80-bit extended sample rate)", _ {
			ByteWriter comm; comm.tag("COMM"); comm.be32(18);
			comm.be16(1);                                     // channels
			comm.be32(64);                                    // num sample frames
			comm.be16(16);                                    // bits
			comm.raw({0x40,0x0E,0xAC,0x44,0x00,0x00,0x00,0x00,0x00,0x00}); // 44100.0 IEEE-754 extended
			ByteWriter ssnd; ssnd.tag("SSND"); ssnd.be32(8 + 64); ssnd.be32(0); ssnd.be32(0); ssnd.zeros(64);
			ByteWriter w; w.tag("FORM"); w.be32(static_cast<uint32_t>(4 + comm.bytes.size() + ssnd.bytes.size()));
			w.tag("AIFF"); w.append(comm); w.append(ssnd);
			Error err; AudioFileFormat f = parse(w.bytes, AudioFileType::SAMPLE, true, err);
			expect(static_cast<int>(err)).to_equal(0);
			expect(static_cast<int>(f.numChannels)).to_equal(1);
			expect(static_cast<int>(f.byteDepth)).to_equal(2);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::ENDIANNESS_WRONG_16));
			expect((int)f.sampleRate).to_equal(44100);
			expect((int)f.audioDataLengthBytes).to_equal(64);
		});
	});
});
// clang-format on

CPPSPEC_SPEC(audio_file_format);
