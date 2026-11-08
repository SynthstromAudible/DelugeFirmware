#include "CppUTest/TestHarness.h"
#include "util/fast_pythag.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace {
constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::min();
constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();

double relativeError(int32_t x, int32_t y) {
	double exact = std::min(std::hypot(static_cast<double>(x), static_cast<double>(y)), static_cast<double>(kInt32Max));
	return std::abs(fastPythag(x, y) - exact) / exact;
}
} // namespace

TEST_GROUP(FastPythagTests){};

TEST(FastPythagTests, zeroAndAxes) {
	CHECK_EQUAL(0, fastPythag(0, 0));
	CHECK_EQUAL(12345, fastPythag(12345, 0));
	CHECK_EQUAL(12345, fastPythag(0, -12345));
	CHECK_EQUAL(5, fastPythag(3, 4));
}

TEST(FastPythagTests, isAMagnitudeNotTheLargerComponent) {
	// It used to index its table by y / (x << 8), which is always 0, so it returned max(|x|, |y|)
	DOUBLES_EQUAL(1.0, fastPythag(1 << 20, 1 << 20) / (std::sqrt(2.0) * (1 << 20)), 0.002);
}

TEST(FastPythagTests, largeInputsDontOverflowTheTableIndex) {
	// Typical unscaled FFT bin sizes - these used to overflow x << 8 and read outside the table
	std::mt19937 rng(1);
	std::uniform_real_distribution<double> phase(0, 2 * M_PI);
	for (int32_t magnitude_bits = 20; magnitude_bits <= 30; magnitude_bits++) {
		for (int32_t n = 0; n < 2000; n++) {
			double magnitude = std::ldexp(1.0, magnitude_bits);
			double angle = phase(rng);
			auto x = static_cast<int32_t>(magnitude * std::cos(angle));
			auto y = static_cast<int32_t>(magnitude * std::sin(angle));
			CHECK(relativeError(x, y) < 0.0015);
		}
	}
}

TEST(FastPythagTests, extremesSaturate) {
	CHECK_EQUAL(kInt32Max, fastPythag(kInt32Min, kInt32Min));
	CHECK_EQUAL(kInt32Max, fastPythag(kInt32Max, kInt32Max));
	CHECK_EQUAL(kInt32Max, fastPythag(kInt32Min, 0));
}
