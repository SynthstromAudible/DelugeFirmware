#include "drivers/usb/usb_audio_pcm.h"

#include "cppspec.hpp"
#include <cstdint>
#include <vector>

namespace {
using namespace deluge::usb_audio;

// Interleaved stereo q31 samples, pseudo-random, plus some at the extremes to exercise rounding and saturation
std::vector<int32_t> makeStereo(size_t numFrames, uint32_t seed) {
	std::vector<int32_t> samples(numFrames * 2);
	uint32_t x = seed;
	for (int32_t& sample : samples) {
		x = x * 1664525u + 1013904223u;
		sample = static_cast<int32_t>(x);
	}
	int32_t const extremes[] = {INT32_MAX,  INT32_MIN,   0x7FFF8000, -0x7FFF8000,
	                            0x00008000, -0x00008000, 0x0000FFFF, 0x003FFC00};
	for (size_t i = 0; i < std::size(extremes) && i < samples.size(); i++) {
		samples[i] = extremes[i];
	}
	return samples;
}

// What the host should get, built one sample at a time. A null bus is silent
std::vector<int16_t> referencePack(std::vector<int32_t> const& main, std::array<std::vector<int32_t> const*, 3> buses,
                                   size_t numFrames) {
	std::vector<int16_t> out;
	for (size_t f = 0; f < numFrames; f++) {
		out.push_back(pcm::narrowSample<pcm::kMainShift>(main[f * 2]));
		out.push_back(pcm::narrowSample<pcm::kMainShift>(main[f * 2 + 1]));
		for (auto const* bus : buses) {
			out.push_back(bus ? pcm::narrowSample<pcm::kTrackBusShift>((*bus)[f * 2]) : int16_t{0});
			out.push_back(bus ? pcm::narrowSample<pcm::kTrackBusShift>((*bus)[f * 2 + 1]) : int16_t{0});
		}
	}
	return out;
}

bool packMatchesReference(size_t numFrames, bool withSilentBus) {
	auto main = makeStereo(numFrames, 1);
	auto bus1 = makeStereo(numFrames, 2);
	auto bus2 = makeStereo(numFrames, 3);
	auto bus3 = makeStereo(numFrames, 4);
	std::array<std::vector<int32_t> const*, 3> buses{&bus1, withSilentBus ? nullptr : &bus2, &bus3};
	std::array<int32_t const*, 3> busPointers{bus1.data(), withSilentBus ? nullptr : bus2.data(), bus3.data()};

	// One spare frame past the end, to catch writing beyond it
	constexpr int16_t kCanary = 0x5A5A;
	std::vector<int16_t> out((numFrames + 1) * 8, kCanary);
	pcm::packToHost(reinterpret_cast<uint32_t*>(out.data()), main.data(), busPointers, numFrames);

	auto expected = referencePack(main, buses, numFrames);
	expected.resize(out.size(), kCanary);
	return out == expected;
}

bool unpackMatchesReference(size_t numFrames) {
	std::vector<int16_t> in(numFrames * 4);
	for (size_t i = 0; i < in.size(); i++) {
		in[i] = static_cast<int16_t>(i * 7919 - 32768);
	}
	constexpr int32_t kCanary = 0x5A5A5A5A;
	std::vector<int32_t> first((numFrames + 1) * 2, kCanary);
	std::vector<int32_t> second((numFrames + 1) * 2, kCanary);
	pcm::unpackFromHost(first.data(), second.data(), in.data(), numFrames);
	for (size_t frame = 0; frame < numFrames; frame++) {
		for (size_t channel = 0; channel < 2; channel++) {
			if (first[frame * 2 + channel] != static_cast<int32_t>(in[frame * 4 + channel]) * 65536
			    || second[frame * 2 + channel] != static_cast<int32_t>(in[frame * 4 + 2 + channel]) * 65536) {
				return false;
			}
		}
	}
	size_t const end = numFrames * 2;
	return first[end] == kCanary && first[end + 1] == kCanary && second[end] == kCanary && second[end + 1] == kCanary;
}

} // namespace

// clang-format off
describe usb_audio_pcm("USB audio multitrack PCM", $ {
	it("rounds and saturates like vqrshrn", _ {
		expect(pcm::narrowSample<16>(INT32_MAX)).to_equal(int16_t{INT16_MAX});
		expect(pcm::narrowSample<16>(INT32_MIN)).to_equal(int16_t{INT16_MIN});
		expect(pcm::narrowSample<16>(0x00008000)).to_equal(int16_t{1});
		expect(pcm::narrowSample<16>(0x00007FFF)).to_equal(int16_t{0});
		expect(pcm::narrowSample<11>(0x10000000)).to_equal(int16_t{INT16_MAX});
	});

	it("packs every length from 0 to 130 frames like the reference", _ {
		bool allMatch = true;
		for (size_t n = 0; n <= 130; n++) {
			allMatch = allMatch && packMatchesReference(n, false);
		}
		expect(allMatch).to_be_true();
	});

	it("packs a missing bus as silence", _ {
		bool allMatch = true;
		for (size_t n = 0; n <= 13; n++) {
			allMatch = allMatch && packMatchesReference(n, true);
		}
		expect(allMatch).to_be_true();
	});

	it("unpacks both pairs, every length from 0 to 130 frames", _ {
		bool allMatch = true;
		for (size_t n = 0; n <= 130; n++) {
			allMatch = allMatch && unpackMatchesReference(n);
		}
		expect(allMatch).to_be_true();
	});
});

CPPSPEC_SPEC(usb_audio_pcm)
