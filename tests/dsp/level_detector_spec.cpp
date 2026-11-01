#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/float/dynamics/level_detector.h"
#include "reference_level_follower.h"

#include <bit>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using namespace deluge::dsp;

namespace {
/// Whether two levels are the same bits: a level can be NaN (a block whose sum wraps negative), and two NaNs from the
/// same arithmetic are the same bits though they never compare equal.
bool same(fp::StereoLevel a, float l, float r) {
	return std::bit_cast<uint32_t>(a.l) == std::bit_cast<uint32_t>(l)
	       && std::bit_cast<uint32_t>(a.r) == std::bit_cast<uint32_t>(r);
}

/// A block of noise frames scaled down by @p shift, so loud and quiet blocks both occur.
std::vector<StereoSample> block(uint32_t seed, std::size_t n, int shift) {
	fixed::SeededNoise noise{seed};
	std::vector<StereoSample> out(n);
	for (auto& f : out) {
		const q31 l = noise.render() >> shift;
		const q31 r = noise.render() >> shift;
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
			const fp::StereoLevel got = detector.measure(frames);
			auto copy = frames;
			const auto want = reference.calcApproxRMS(copy);
			mismatches += same(got, want.l, want.r) ? 0 : 1;
		}
		expect(mismatches).to_equal(0);
	});

	it("starts from silence after a reset", _ {
		fp::LevelDetector detector;
		std::ignore = detector.measure(block(1, 128, 0));
		detector.reset();
		fp::LevelDetector fresh;
		const auto frames = block(2, 128, 12);
		const fp::StereoLevel a = detector.measure(frames);
		const fp::StereoLevel b = fresh.measure(frames);
		expect(same(a, b.l, b.r)).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(level_detector);
