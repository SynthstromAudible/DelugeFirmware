#include "engines/effects/bitcrush.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

constexpr int32_t kOff = std::numeric_limits<int32_t>::min();
/// Bitcrush preset 6 of 7: (6 << 29) - 2^31.
constexpr int32_t kPresetSix = 1073741824;
/// Sample-rate reduction a quarter of the way up.
constexpr int32_t kQuarterRate = -1073741824;

std::vector<StereoSample> wave(size_t frames) {
	std::vector<StereoSample> block;
	for (size_t i = 0; i < frames; i++) {
		const auto v = static_cast<int32_t>((i * 2654435761u) & 0x3FFFFFFFu) - (1 << 29);
		block.push_back({.l = v, .r = -v});
	}
	return block;
}

bool same(const std::vector<StereoSample>& a, const std::vector<StereoSample>& b) {
	return std::ranges::equal(a, b, [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; });
}

} // namespace

// clang-format off
describe bitcrush("bit depth and sample-rate reduction", ${
	it("leaves a block and its volume alone when both are off", _ {
		Bitcrush b;
		std::vector<StereoSample> block = wave(32);
		int32_t volume = 1 << 27;
		volume >>= b.process(block, {.bitcrush = kOff, .sample_rate_reduction = kOff}).volume_shift;
		expect(same(block, wave(32))).to_be_true();
		expect(volume).to_equal(1 << 27);
	});
	it("masks the low bits and lowers the volume at a strong bitcrush", _ {
		Bitcrush b;
		std::vector<StereoSample> block = wave(32);
		int32_t volume = 1 << 27;
		volume >>= b.process(block, {.bitcrush = kPresetSix, .sample_rate_reduction = kOff}).volume_shift;
		const std::vector<StereoSample> input = wave(32);
		const uint32_t mask = 0xFFFFFFFFu << 25;
		for (size_t i = 0; i < block.size(); i++) {
			expect(block[i].l).to_equal(static_cast<int32_t>(static_cast<uint32_t>(input[i].l) & mask));
		}
		expect(volume).to_equal((1 << 27) >> 2);
	});
	it("restarts sample-rate reduction each time it is switched on", _ {
		Bitcrush b;
		const BitcrushParams on{.bitcrush = kOff, .sample_rate_reduction = kQuarterRate};
		const BitcrushParams off{.bitcrush = kOff, .sample_rate_reduction = kOff};
		int32_t volume = 1 << 27;
		std::vector<StereoSample> first = wave(32);
		volume >>= b.process(first, on).volume_shift;
		std::vector<StereoSample> between = wave(32);
		volume >>= b.process(between, off).volume_shift;
		std::vector<StereoSample> again = wave(32);
		volume >>= b.process(again, on).volume_shift;
		expect(same(first, again)).to_be_true();
	});
	it("carries sample-rate reduction across block boundaries", _ {
		const BitcrushParams on{.bitcrush = kOff, .sample_rate_reduction = kQuarterRate};
		int32_t volume = 1 << 27;
		Bitcrush whole;
		std::vector<StereoSample> one = wave(64);
		volume >>= whole.process(one, on).volume_shift;
		Bitcrush halves;
		std::vector<StereoSample> two = wave(64);
		volume >>= halves.process(std::span{two}.first(32), on).volume_shift;
		volume >>= halves.process(std::span{two}.last(32), on).volume_shift;
		expect(same(one, two)).to_be_true();
	});
});

CPPSPEC_SPEC(bitcrush)
