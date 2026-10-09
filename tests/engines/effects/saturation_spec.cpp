#include "engines/effects/saturation.h"

#include "dsp-next/fixed/shaping/legacy/antialiased_tanh.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

constexpr int32_t kInput = 1 << 28;

/// The voice variant of one first sample, by hand: tanh at 5 + amount, shifted from amount 2.
int32_t voice_by_hand(uint8_t amount) {
	deluge::dsp::fixed::legacy::AntialiasedTanh curve{5u + amount};
	return curve.render(kInput) << (amount >= 2 ? amount - 2 : 0);
}

/// The chain variant of one first sample, by hand: tanh at 3 + amount, shifted from amount 3.
int32_t chain_by_hand(uint8_t amount) {
	deluge::dsp::fixed::legacy::AntialiasedTanh curve{3u + amount};
	return curve.render(kInput) << (amount >= 3 ? amount - 3 : 0);
}

std::vector<StereoSample> ramp() {
	std::vector<StereoSample> block;
	for (int32_t i = 0; i < 16; i++) {
		block.push_back({.l = i * (1 << 25), .r = -i * (1 << 24)});
	}
	return block;
}

/// Whether two blocks hold the same samples.
bool same(const std::vector<StereoSample>& a, const std::vector<StereoSample>& b) {
	return std::ranges::equal(a, b, [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; });
}

} // namespace

// clang-format off
describe saturation("tanh saturation per voice and per chain", ${
	it("saturates a voice's sample harder, shifting from amount 2", _ {
		for (uint8_t amount = 1; amount <= 4; amount++) {
			Saturation s;
			expect(s.saturate(kInput, 0, {.clipping_amount = amount, .variant = SaturationVariant::VOICE}))
			    .to_equal(voice_by_hand(amount));
		}
	});
	it("saturates a chain's sample from amount 1, shifting from amount 3", _ {
		for (uint8_t amount = 1; amount <= 4; amount++) {
			Saturation s;
			expect(s.saturate(kInput, 0, {.clipping_amount = amount, .variant = SaturationVariant::CHAIN}))
			    .to_equal(chain_by_hand(amount));
		}
	});
	it("leaves a chain's block untouched at amount 0", _ {
		Saturation s;
		std::vector<StereoSample> block = ramp();
		s.process(block, {.clipping_amount = 0, .variant = SaturationVariant::CHAIN});
		expect(same(block, ramp())).to_be_true();
	});
	it("changes a chain's block at amount 1", _ {
		Saturation s;
		std::vector<StereoSample> block = ramp();
		s.process(block, {.clipping_amount = 1, .variant = SaturationVariant::CHAIN});
		expect(same(block, ramp())).to_be_false();
	});
	it("keeps each channel's antialiasing memory apart", _ {
		Saturation byBlock;
		Saturation bySample;
		const SaturationParams params{.clipping_amount = 4, .variant = SaturationVariant::CHAIN};
		std::vector<StereoSample> block = ramp();
		byBlock.process(block, params);
		const std::vector<StereoSample> input = ramp();
		for (size_t i = 0; i < input.size(); i++) {
			expect(block[i].l).to_equal(bySample.saturate(input[i].l, 0, params));
			expect(block[i].r).to_equal(bySample.saturate(input[i].r, 1, params));
		}
	});
});

CPPSPEC_SPEC(saturation)
