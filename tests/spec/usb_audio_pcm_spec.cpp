#include "drivers/usb/usb_audio_pcm.h"
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

namespace {
using namespace deluge::usb_audio;

// Interleaved stereo q31 samples, all different, with all four bytes non-zero
std::vector<int32_t> makeStereo(size_t numFrames, uint32_t seed) {
	std::vector<int32_t> samples(numFrames * 2);
	uint32_t x = seed;
	for (int32_t& sample : samples) {
		x = x * 1664525u + 1013904223u;
		sample = static_cast<int32_t>(x | 0x01010101u);
	}
	return samples;
}

// The bytes USB audio expects, built one sample at a time
std::vector<uint8_t> referencePack(std::vector<int32_t> const& a, std::vector<int32_t> const& b, size_t numFrames) {
	std::vector<uint8_t> out;
	for (size_t f = 0; f < numFrames; f++) {
		for (int32_t sample : {a[f * 2], a[f * 2 + 1], b[f * 2], b[f * 2 + 1]}) {
			auto u = static_cast<uint32_t>(sample);
			out.push_back(u >> 8);
			out.push_back(u >> 16);
			out.push_back(u >> 24);
		}
	}
	return out;
}

bool packMatchesReference(size_t numFrames) {
	auto a = makeStereo(numFrames, 1);
	auto b = makeStereo(numFrames, 2);
	// One spare frame's worth past the end, to catch writing beyond it
	std::vector<uint8_t> out((numFrames + 1) * pcm::kBytesPerFrame, 0xAA);
	pcm::pack(out.data(), a.data(), b.data(), numFrames);
	auto expected = referencePack(a, b, numFrames);
	expected.resize(out.size(), 0xAA);
	return out == expected;
}

bool unpackInvertsPack(size_t numFrames) {
	auto a = makeStereo(numFrames, 3);
	auto b = makeStereo(numFrames, 4);
	std::vector<uint8_t> packed(numFrames * pcm::kBytesPerFrame);
	pcm::pack(packed.data(), a.data(), b.data(), numFrames);

	std::vector<int32_t> outA(numFrames * 2 + 2, 0x55555555);
	std::vector<int32_t> outB(numFrames * 2 + 2, 0x55555555);
	pcm::unpack(outA.data(), outB.data(), packed.data(), numFrames);

	for (size_t i = 0; i < numFrames * 2; i++) {
		// The low byte is below the 24 bits, so comes back as zero
		if (outA[i] != (a[i] & ~0xFF) || outB[i] != (b[i] & ~0xFF)) {
			return false;
		}
	}
	// Nothing written past the end
	return outA[numFrames * 2] == 0x55555555 && outB[numFrames * 2] == 0x55555555;
}

constexpr size_t kFrameCounts[] = {0, 1, 3, 4, 5, 8, 43, 44, 45, 46, 48};
} // namespace

// clang-format off
describe usb_audio_pcm("deluge::usb_audio::pcm", ${
	it("packs two stereo sources into 4-channel 24-bit frames", _ {
		for (size_t numFrames : kFrameCounts) {
			expect(packMatchesReference(numFrames)).to_be_true();
		}
	});

	it("puts the first source on channels 1/2 and the second on 3/4", _ {
		int32_t const a[2] = {0x11223344, 0x55667788};
		int32_t const b[2] = {static_cast<int32_t>(0x99AABBCC), static_cast<int32_t>(0xDDEEFF00)};
		uint8_t out[pcm::kBytesPerFrame];
		pcm::pack(out, a, b, 1);
		std::vector<uint8_t> const expected = {0x33, 0x22, 0x11, 0x77, 0x66, 0x55,
		                                       0xBB, 0xAA, 0x99, 0xFF, 0xEE, 0xDD};
		expect(std::vector<uint8_t>(out, out + pcm::kBytesPerFrame) == expected).to_be_true();
	});

	it("unpacks what it packed, to 24 bits", _ {
		for (size_t numFrames : kFrameCounts) {
			expect(unpackInvertsPack(numFrames)).to_be_true();
		}
	});
});

CPPSPEC_SPEC(usb_audio_pcm);
