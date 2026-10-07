#include "engines/effects/equalizer.h"

#include <algorithm>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

std::vector<StereoSample> sine(size_t frames, size_t offset = 0) {
	std::vector<StereoSample> block;
	for (size_t i = offset; i < offset + frames; i++) {
		const auto v = static_cast<int32_t>(std::sin(static_cast<double>(i) * 0.01) * (1 << 28));
		block.push_back({.l = v, .r = v});
	}
	return block;
}

bool same(const std::vector<StereoSample>& a, const std::vector<StereoSample>& b) {
	return std::ranges::equal(a, b, [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; });
}

int64_t level(const std::vector<StereoSample>& block) {
	int64_t sum = 0;
	for (const StereoSample& s : block) {
		sum += std::abs(static_cast<int64_t>(s.l));
	}
	return sum;
}

constexpr EqualizerParams kFlat{};
constexpr EqualizerParams kBassBoost{.bass = 0x30000000, .bass_frequency = 0x10000000};

} // namespace

// clang-format off
describe equalizer("the two-band shelving EQ", ${
	it("leaves a block alone when both bands are flat", _ {
		Equalizer eq;
		std::vector<StereoSample> block = sine(64);
		eq.process(block, kFlat);
		expect(same(block, sine(64))).to_be_true();
	});
	it("raises a slow wave with a bass boost", _ {
		Equalizer eq;
		std::vector<StereoSample> block = sine(512);
		eq.process(block, kBassBoost);
		expect(level(block) > level(sine(512))).to_be_true();
	});
	it("keeps a band's filter memory while it is flat", _ {
		Equalizer withFlatBlock;
		std::vector<StereoSample> a = sine(64);
		withFlatBlock.process(a, kBassBoost);
		std::vector<StereoSample> b = sine(64, 64);
		withFlatBlock.process(b, kFlat);
		std::vector<StereoSample> c = sine(64, 128);
		withFlatBlock.process(c, kBassBoost);

		Equalizer withoutIt;
		std::vector<StereoSample> a2 = sine(64);
		withoutIt.process(a2, kBassBoost);
		std::vector<StereoSample> c2 = sine(64, 128);
		withoutIt.process(c2, kBassBoost);
		expect(same(c, c2)).to_be_true();
	});
});

CPPSPEC_SPEC(equalizer)
