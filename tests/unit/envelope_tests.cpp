#include "CppUTest/TestHarness.h"
#include "util/fixedpoint.h"
#include <algorithm>
#include <cstdint>

namespace {

// Standalone reimplementation of the sustain smoothing from Envelope::render().
// Tests the fix for #4355: smoothedSustain going negative when sustain=0.
struct EnvelopeSustainSim {
	int32_t smoothedSustain{0};

	// Mirrors the smoothing calculation in Envelope::render() DECAY/SUSTAIN stages
	void step(uint32_t numSamples, uint32_t sustain) {
		smoothedSustain = add_saturate(smoothedSustain, numSamples * (((int32_t)sustain - smoothedSustain) >> 9));
		smoothedSustain = std::max<int32_t>(smoothedSustain, 0);
	}
};

TEST_GROUP(EnvelopeSustain){};

// With sustain=0, smoothedSustain must never go negative
TEST(EnvelopeSustain, zeroSustainNeverNegative) {
	EnvelopeSustainSim env;
	env.smoothedSustain = 100; // small residual from prior decay
	for (int i = 0; i < 2000; i++) {
		env.step(1, 0);
		CHECK(env.smoothedSustain >= 0);
	}
	CHECK_EQUAL(0, env.smoothedSustain);
}

// Without the clamp, large numSamples can overshoot negative
TEST(EnvelopeSustain, largeBlockSizeNeverNegative) {
	EnvelopeSustainSim env;
	env.smoothedSustain = 50;
	env.step(512, 0); // large block
	CHECK(env.smoothedSustain >= 0);
}

// Smoothing toward a nonzero sustain converges without going negative
TEST(EnvelopeSustain, convergesToNonzeroSustain) {
	EnvelopeSustainSim env;
	env.smoothedSustain = 0;
	uint32_t sustain = 1000000;
	for (int i = 0; i < 10000; i++) {
		env.step(1, sustain);
	}
	// Should converge close to sustain
	CHECK(env.smoothedSustain > 0);
	int32_t diff = (int32_t)sustain - env.smoothedSustain;
	CHECK(diff >= 0);
	CHECK(diff < (int32_t)(sustain / 100)); // within 1%
}

} // namespace
