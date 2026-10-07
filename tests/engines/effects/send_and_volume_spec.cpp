#include "engines/effects/send_and_volume.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

std::vector<StereoSample> constant(size_t frames, int32_t l, int32_t r) {
	return std::vector<StereoSample>(frames, StereoSample{.l = l, .r = r});
}

bool same(const std::vector<StereoSample>& a, const std::vector<StereoSample>& b) {
	return std::ranges::equal(a, b, [](StereoSample x, StereoSample y) { return x.l == y.l && x.r == y.r; });
}

constexpr int32_t kVolume = 134217728; // unity post-FX and post-reverb volume

} // namespace

// clang-format off
describe send_and_volume("the reverb send, post-FX volume and pan", ${
	it("sends nothing at a send of 0", _ {
		SendAndVolume s;
		std::vector<StereoSample> block = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		const bool sent = s.process(block, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume});
		expect(sent).to_be_false();
		expect(std::ranges::all_of(reverb, [](int32_t v) { return v == 0; })).to_be_true();
	});
	it("adds the send to the reverb", _ {
		SendAndVolume s;
		std::vector<StereoSample> block = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		const bool sent = s.process(block, reverb,
		                            {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .reverb_send = 1 << 30});
		expect(sent).to_be_true();
		expect(reverb[0] != 0).to_be_true();
	});
	it("ignores pan when rendering in mono", _ {
		SendAndVolume panned;
		SendAndVolume centred;
		std::vector<StereoSample> a = constant(16, 1 << 28, 1 << 28);
		std::vector<StereoSample> b = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		(void)panned.process(a, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .pan = 1 << 29,
		                                 .stereo = false});
		(void)centred.process(b, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .stereo = false});
		expect(same(a, b)).to_be_true();
	});
	it("scales both channels alike at the centre and favours one side when panned in stereo", _ {
		SendAndVolume s;
		std::vector<StereoSample> block = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		(void)s.process(block, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume});
		expect(block[0].l).to_equal(block[0].r);
		SendAndVolume right;
		std::vector<StereoSample> panned = constant(16, 1 << 28, 1 << 28);
		(void)right.process(panned, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .pan = 1 << 29});
		expect(panned[0].r > panned[0].l).to_be_true();
	});
	it("pans a stereo signal to exact gains at a mid pan", _ {
		SendAndVolume s;
		std::vector<StereoSample> block = constant(4, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(4, 0);
		(void)s.process(block, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .pan = 0x10000000});
		expect(block[0].l).to_equal(201326592);
		expect(block[0].r).to_equal(268435424);
	});
	it("ramps a panned stereo signal by exact steps", _ {
		SendAndVolume s{100000000};
		std::vector<StereoSample> block = constant(4, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(4, 0);
		(void)s.process(block, reverb,
		                {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .pan = 0x10000000, .ramp = true});
		const std::vector<int32_t> left{block[0].l, block[1].l, block[2].l, block[3].l};
		const std::vector<int32_t> right{block[0].r, block[1].r, block[2].r, block[3].r};
		expect(left).to_equal(std::vector<int32_t>{214158240, 226989888, 239821536, 252653184});
		expect(right).to_equal(std::vector<int32_t>{285544288, 302653152, 319762016, 336870848});
	});
	it("ramps from a primed volume without a jump", _ {
		SendAndVolume s{-1};
		s.prime(kVolume);
		std::vector<StereoSample> block = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		(void)s.process(block, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .ramp = true});
		expect(block.front().l).to_equal(block.back().l);
	});
	it("ramps from the last block's volume", _ {
		SendAndVolume s{-1};
		s.prime(kVolume / 2);
		std::vector<StereoSample> block = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		(void)s.process(block, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .ramp = true});
		expect(block.front().l < block.back().l).to_be_true();
	});
	it("leaves a primed volume alone once it has one", _ {
		SendAndVolume s{kVolume};
		s.prime(kVolume / 2);
		std::vector<StereoSample> block = constant(16, 1 << 28, 1 << 28);
		std::vector<int32_t> reverb(16, 0);
		(void)s.process(block, reverb, {.post_fx_volume = kVolume, .post_reverb_volume = kVolume, .ramp = true});
		expect(block.front().l).to_equal(block.back().l);
	});
});

CPPSPEC_SPEC(send_and_volume)
