#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/float/dynamics/level_detector.h"
#include "reference_level_follower.h"

#include <bit>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using namespace deluge::dsp;

namespace {
/// Whether two levels are the same bits.
bool same(fp::StereoLevel a, float l, float r) {
	return std::bit_cast<uint32_t>(a.l) == std::bit_cast<uint32_t>(l)
	       && std::bit_cast<uint32_t>(a.r) == std::bit_cast<uint32_t>(r);
}

/// A block of noise frames scaled down by @p shift, so loud and quiet blocks both occur.
std::vector<test::RawFrame> block(uint32_t seed, std::size_t n, int shift) {
	fixed::SeededNoise noise{seed};
	std::vector<test::RawFrame> out(n);
	for (auto& f : out) {
		const int32_t l = noise.render() >> shift;
		const int32_t r = noise.render() >> shift;
		f = {.l = l, .r = r};
	}
	return out;
}
} // namespace

// clang-format off
describe level_detector("the level detector", ${
	it("measures each block as the app's follower always has", _ {
		fp::LevelDetector detector;
		ReferenceLevelFollower reference;
		int32_t mismatches = 0;
		for (int b = 0; b < 200; b++) {
			const auto frames = block(static_cast<uint32_t>(b), (b % 3 == 0) ? 128 : 37, b % 12);
			const fp::StereoLevel got = detector.measure(test::typed_frames(std::span{frames}));
			auto copy = frames;
			const auto want = reference.calcApproxRMS(copy);
			mismatches += same(got, want.l, want.r) ? 0 : 1;
		}
		expect(mismatches).to_equal(0);
	});

	it("reads a full-scale block near ln 2^31, its sums not overflowing", _ {
		fp::LevelDetector detector;
		const std::vector<test::RawFrame> frames(128, test::RawFrame{.l = INT32_MIN, .r = INT32_MAX});
		fp::StereoLevel level{};
		for (int b = 0; b < 20; b++) {
			level = detector.measure(test::typed_frames(std::span{frames}));
		}
		expect(level.l > 21.4f && level.l < 21.5f).to_be_true();
		expect(level.r > 21.4f && level.r < 21.5f).to_be_true();
	});

	it("measures a block from the caller's means as from its frames", _ {
		fp::LevelDetector from_frames;
		fp::LevelDetector from_means;
		const auto frames = block(3, 128, 4);
		int64_t sum_l = 0;
		int64_t sum_r = 0;
		for (const test::RawFrame f : frames) {
			sum_l += (f.l ^ (f.l >> 31)) >> 8;
			sum_r += (f.r ^ (f.r >> 31)) >> 8;
		}
		const fp::StereoLevel a = from_frames.measure(test::typed_frames(std::span{frames}));
		const fp::StereoLevel b = from_means.measure_means(256.f * static_cast<float>(sum_l) / 128.f,
		                                                   256.f * static_cast<float>(sum_r) / 128.f, 128);
		expect(same(a, b.l, b.r)).to_be_true();
	});

	it("starts from silence after a reset", _ {
		fp::LevelDetector detector;
		const auto warm = block(1, 128, 0);
		std::ignore = detector.measure(test::typed_frames(std::span{warm}));
		detector.reset();
		fp::LevelDetector fresh;
		const auto frames = block(2, 128, 12);
		const fp::StereoLevel a = detector.measure(test::typed_frames(std::span{frames}));
		const fp::StereoLevel b = fresh.measure(test::typed_frames(std::span{frames}));
		expect(same(a, b.l, b.r)).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(level_detector);
