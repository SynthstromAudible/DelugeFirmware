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

// Descriptor regression net for the WAV/AIFF header parser (parseSampleHeader / parseWaveTableHeader). Feeds
// hand-crafted byte buffers through an in-memory AudioByteSource and asserts each parsed header field against
// a value derived from first principles (the WAV/AIFF spec) — independent of the parser implementation, so it
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

// In-memory byte source: a forward cursor, a strict past-EOF check (FILE_CORRUPTED), and an absolute forward
// seek. The buffer includes the 12-byte RIFF/FORM container header, which the parser now reads itself, so the
// cursor starts at 0.
class MemoryByteSource final : public AudioByteSource {
public:
	explicit MemoryByteSource(std::span<const std::byte> data) : data_(data), pos_(0) {}

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

constexpr int rawFmt(RawDataFormat f) {
	return static_cast<int>(util::to_underlying(f));
}

std::expected<SampleHeader, Error> parseSample(const std::vector<std::byte>& file) {
	MemoryByteSource src{file};
	return parseSampleHeader(src);
}

std::expected<WaveTableHeader, Error> parseWaveTable(const std::vector<std::byte>& file, bool atAllCosts = false) {
	MemoryByteSource src{file};
	return parseWaveTableHeader(src, atAllCosts);
}

} // namespace

// clang-format off
describe audio_file_format("audio file header parser", $ {
	context("WAV samples", _ {
		it("decodes 16-bit stereo PCM", _ {
			ByteWriter body; body.append(wavFmt(16, 2, 44100)); body.append(wavData(100));
			auto r = parseSample(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			const SampleHeader& f = *r;
			expect(static_cast<int>(f.numChannels)).to_equal(2);
			expect(static_cast<int>(f.byteDepth)).to_equal(2);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::NATIVE));
			expect((int)f.sampleRate).to_equal(44100);
			expect((int)f.audioDataStartPosBytes).to_equal(44); // 12 top +8 fmt hdr +16 fmt +8 data hdr
			expect((int)f.audioDataLengthBytes).to_equal(100);
			expect(f.midiNote.has_value()).to_equal(false);
			expect((int)f.fileLoopStartSamples).to_equal(0);
			expect((int)f.waveTableCycleSize).to_equal(2048);
			expect(f.fileExplicitlySpecifiesSelfAsWaveTable).to_equal(false);
		});

		it("decodes 8-bit mono as UNSIGNED_8", _ {
			ByteWriter body; body.append(wavFmt(8, 1, 22050)); body.append(wavData(50));
			auto r = parseSample(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			const SampleHeader& f = *r;
			expect(static_cast<int>(f.numChannels)).to_equal(1);
			expect(static_cast<int>(f.byteDepth)).to_equal(1);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::UNSIGNED_8));
			expect((int)f.sampleRate).to_equal(22050);
		});

		it("decodes 24-bit mono PCM", _ {
			ByteWriter body; body.append(wavFmt(24, 1, 48000)); body.append(wavData(30));
			auto r = parseSample(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			expect(static_cast<int>(r->byteDepth)).to_equal(3);
			expect(rawFmt(r->rawDataFormat)).to_equal(rawFmt(RawDataFormat::NATIVE));
		});

		it("decodes 32-bit float as FLOAT", _ {
			ByteWriter body; body.append(wavFmt(32, 1, 44100, 3 /*IEEE float*/)); body.append(wavData(40));
			auto r = parseSample(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			expect(static_cast<int>(r->byteDepth)).to_equal(4);
			expect(rawFmt(r->rawDataFormat)).to_equal(rawFmt(RawDataFormat::FLOAT));
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
			auto r = parseSample(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			expect(r->midiNote.has_value()).to_equal(true);
			expect(*r->midiNote).to_equal(60.0f);
			expect((int)r->fileLoopStartSamples).to_equal(1000);
			expect((int)r->fileLoopEndSamples).to_equal(2000);
		});

		it("returns FILE_CORRUPTED on a truncated fmt chunk", _ {
			ByteWriter body; body.tag("fmt "); body.le32(16); body.le32(0); // claims 16, supplies 4
			auto r = parseSample(wrapWav(body));
			expect(r.has_value()).to_equal(false);
			expect(static_cast<int>(r.error())).to_equal(static_cast<int>(Error::FILE_CORRUPTED));
		});
	});

	context("WAV wavetables", _ {
		it("accepts a clm-tagged single-cycle file and reports the cycle size", _ {
			ByteWriter clm; clm.tag("clm "); clm.le32(7); clm.raw({'<','!','>','2','0','4','8'});
			clm.u8(0); // RIFF pad byte: odd (7-byte) chunk is padded to an even boundary
			ByteWriter body; body.append(wavFmt(16, 1, 44100)); body.append(clm); body.append(wavData(4096));
			auto r = parseWaveTable(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			expect((int)r->cycleSize).to_equal(2048);
			expect((int)r->audioDataLengthBytes).to_equal(4096);
		});

		it("rejects a stereo file as a wavetable", _ {
			ByteWriter body; body.append(wavFmt(16, 2, 44100)); body.append(wavData(4096));
			auto r = parseWaveTable(wrapWav(body));
			expect(r.has_value()).to_equal(false);
			expect(static_cast<int>(r.error()))
			    .to_equal(static_cast<int>(Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO));
		});
	});

	// Whole-header pin: one file exercising every SampleHeader/WaveTableHeader field at once, so a
	// re-implementation of the parse behind these two entry points has to reproduce all of them
	// together rather than one facet per example. Deliberately puts `smpl` AFTER `data` — the layout
	// 35% of the real corpus uses — so the pin covers a header field that only a parser willing to
	// walk past the audio payload can reach.
	context("full-header pin", _ {
		// RIFF(12) + fmt(8+16) + data(8+216) + clm(8+7+1 pad) + smpl(8+60).
		auto pinFile = [] {
			ByteWriter smpl; smpl.tag("smpl"); smpl.le32(60);
			smpl.le32(0); smpl.le32(0); smpl.le32(0);       // manuf, product, samplePeriod
			smpl.le32(57);                                  // MIDI unity note
			smpl.le32(0x80000000);                          // pitch fraction == half a semitone
			smpl.le32(0); smpl.le32(0);                     // smpteFormat, smpteOffset
			smpl.le32(1);                                   // num sample loops
			smpl.le32(0);                                   // sampler data
			smpl.le32(0); smpl.le32(0);                     // loop: id, type
			smpl.le32(1234);                                // loop start
			smpl.le32(5678);                                // loop end
			smpl.le32(0); smpl.le32(0);                     // fraction, playCount
			ByteWriter clm; clm.tag("clm "); clm.le32(7); clm.raw({'<','!','>','1','0','2','4'});
			clm.u8(0);                                      // RIFF pad byte for the odd-length chunk
			ByteWriter body;
			body.append(wavFmt(24, 2, 48000));
			body.append(wavData(216));
			body.append(clm);
			body.append(smpl);
			return wrapWav(body);
		};

		it("pins every SampleHeader field", _ {
			auto r = parseSample(pinFile());
			expect(r.has_value()).to_equal(true);
			const SampleHeader& f = *r;
			expect(static_cast<int>(f.numChannels)).to_equal(2);
			expect(static_cast<int>(f.byteDepth)).to_equal(3);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::NATIVE));
			expect((int)f.sampleRate).to_equal(48000);
			expect((int)f.audioDataStartPosBytes).to_equal(44); // 12 top + 8+16 fmt + 8 data hdr
			expect((int)f.audioDataLengthBytes).to_equal(216);
			expect((int)f.fileLoopStartSamples).to_equal(1234);
			expect((int)f.fileLoopEndSamples).to_equal(5678);
			expect(f.midiNote.has_value()).to_equal(true);
			expect(*f.midiNote).to_equal(57.5f);
			expect((int)f.waveTableCycleSize).to_equal(1024);
			expect(f.fileExplicitlySpecifiesSelfAsWaveTable).to_equal(true);
		});

		it("pins every WaveTableHeader field", _ {
			ByteWriter clm; clm.tag("clm "); clm.le32(7); clm.raw({'<','!','>','1','0','2','4'});
			clm.u8(0);
			ByteWriter body; body.append(wavFmt(16, 1, 44100)); body.append(clm); body.append(wavData(4096));
			auto r = parseWaveTable(wrapWav(body));
			expect(r.has_value()).to_equal(true);
			const WaveTableHeader& f = *r;
			expect(static_cast<int>(f.numChannels)).to_equal(1);
			expect(static_cast<int>(f.byteDepth)).to_equal(2);
			expect(rawFmt(f.rawDataFormat)).to_equal(rawFmt(RawDataFormat::NATIVE));
			expect((int)f.audioDataStartPosBytes).to_equal(60); // 12 top + 8+16 fmt + 16 clm + 8 data hdr
			expect((int)f.audioDataLengthBytes).to_equal(4096);
			expect((int)f.cycleSize).to_equal(1024);
		});

		// The three wavetable rejections, pinned as the exact Error values callers switch on.
		it("rejects an untagged non-wavetable-length file unless the caller insists", _ {
			ByteWriter body; body.append(wavFmt(16, 1, 44100)); body.append(wavData(100));
			auto r = parseWaveTable(wrapWav(body));
			expect(r.has_value()).to_equal(false);
			expect(static_cast<int>(r.error())).to_equal(static_cast<int>(Error::FILE_NOT_LOADABLE_AS_WAVETABLE));

			auto insisted = parseWaveTable(wrapWav(body), true);
			expect(insisted.has_value()).to_equal(true);
			expect((int)insisted->cycleSize).to_equal(2048);
		});

		it("rejects an AIFF file as a wavetable unless the caller insists", _ {
			ByteWriter comm; comm.tag("COMM"); comm.be32(18);
			comm.be16(1); comm.be32(32); comm.be16(16);
			comm.raw({0x40,0x0E,0xAC,0x44,0x00,0x00,0x00,0x00,0x00,0x00}); // 44100.0
			ByteWriter ssnd; ssnd.tag("SSND"); ssnd.be32(8 + 4096); ssnd.be32(0); ssnd.be32(0); ssnd.zeros(4096);
			ByteWriter w; w.tag("FORM"); w.be32(static_cast<uint32_t>(4 + comm.bytes.size() + ssnd.bytes.size()));
			w.tag("AIFF"); w.append(comm); w.append(ssnd);
			auto r = parseWaveTable(w.bytes);
			expect(r.has_value()).to_equal(false);
			expect(static_cast<int>(r.error())).to_equal(static_cast<int>(Error::FILE_NOT_LOADABLE_AS_WAVETABLE));

			auto insisted = parseWaveTable(w.bytes, true);
			expect(insisted.has_value()).to_equal(true);
			expect((int)insisted->audioDataLengthBytes).to_equal(4096);
		});
	});

	// Two shapes that loaded before the parse moved to Rust and must keep loading. Neither is in the
	// sample corpus the port was differentially tested against, which is exactly why they are pinned
	// here: "not in one person's library" is not the same as "cannot happen".
	context("files that must keep loading", _ {
		// Trailing slop -- DAW padding, an interrupted copy, a card that filled mid-write -- leaves a
		// few bytes after the last chunk that cannot hold an 8-byte chunk header. The walk must end
		// there and report the header it found, not call the file corrupt. 1..7 is exactly the range
		// where the old parser's 8-byte read gave up.
		it("decodes a file with a tail too short to hold a chunk header", _ {
			ByteWriter body; body.append(wavFmt(16, 2, 44100)); body.append(wavData(100));
			const std::vector<std::byte> clean = wrapWav(body);
			auto reference = parseSample(clean);
			expect(reference.has_value()).to_equal(true);

			for (int tail = 1; tail <= 7; tail++) {
				std::vector<std::byte> slopped = clean;
				for (int i = 0; i < tail; i++) {
					slopped.push_back(static_cast<std::byte>(0xAA));
				}
				auto r = parseSample(slopped);
				expect(r.has_value()).to_equal(true);
				expect((int)r->audioDataStartPosBytes).to_equal((int)reference->audioDataStartPosBytes);
				expect((int)r->audioDataLengthBytes).to_equal((int)reference->audioDataLengthBytes);
				expect((int)r->sampleRate).to_equal((int)reference->sampleRate);
				expect(static_cast<int>(r->numChannels)).to_equal(static_cast<int>(reference->numChannels));
				expect(static_cast<int>(r->byteDepth)).to_equal(static_cast<int>(reference->byteDepth));
			}
		});

		// An AIFF is never a wavetable unless the user insists, and that verdict is about the CONTAINER,
		// not about whether the file decodes: sample_browser.cpp turns FILE_NOT_LOADABLE_AS_WAVETABLE
		// into "load it as a Sample instead", and any other error into "give up". This file decodes fine
		// as a Sample but not as a wavetable-shaped walk (its SSND comes before its COMM, so a walk that
		// stops at the audio data has no bit depth yet) -- so a decode-shaped error here would stop it
		// being offered as a Sample at all.
		it("offers an AIFF whose SSND precedes its COMM as a sample, not as a corrupt file", _ {
			ByteWriter comm; comm.tag("COMM"); comm.be32(18);
			comm.be16(1); comm.be32(32); comm.be16(16);
			comm.raw({0x40,0x0E,0xAC,0x44,0x00,0x00,0x00,0x00,0x00,0x00}); // 44100.0
			ByteWriter ssnd; ssnd.tag("SSND"); ssnd.be32(8 + 4096); ssnd.be32(0); ssnd.be32(0); ssnd.zeros(4096);
			ByteWriter w; w.tag("FORM"); w.be32(static_cast<uint32_t>(4 + ssnd.bytes.size() + comm.bytes.size()));
			w.tag("AIFF"); w.append(ssnd); w.append(comm); // SSND first, then COMM.

			auto asWaveTable = parseWaveTable(w.bytes);
			expect(asWaveTable.has_value()).to_equal(false);
			expect(static_cast<int>(asWaveTable.error()))
			    .to_equal(static_cast<int>(Error::FILE_NOT_LOADABLE_AS_WAVETABLE));

			auto asSample = parseSample(w.bytes);
			expect(asSample.has_value()).to_equal(true);
			expect((int)asSample->audioDataLengthBytes).to_equal(4096);
			expect((int)asSample->sampleRate).to_equal(44100);
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
			auto r = parseSample(w.bytes);
			expect(r.has_value()).to_equal(true);
			const SampleHeader& f = *r;
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
