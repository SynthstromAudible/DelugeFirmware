#include "CppUTest/TestHarness.h"
#include "util/fast_divide.h"
#include <cstdint>
#include <limits>
#include <random>

using deluge::divideBySmall;

namespace {
constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::min();
constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();
} // namespace

TEST_GROUP(FastDivideTests){};

// divideBySmall() has to give exactly what the operator gives, rounding towards zero, for every dividend
TEST(FastDivideTests, edgesForEveryDivisor) {
	for (int32_t divisor = 1; divisor <= 300; divisor++) { // past 256 it falls back to the operator
		for (int32_t x : {0, 1, -1, kInt32Max, kInt32Min, kInt32Min + 1, divisor, -divisor, divisor - 1, -(divisor - 1),
		                  divisor + 1, -(divisor + 1)}) {
			CHECK_EQUAL(x / divisor, divideBySmall(x, divisor));
		}
	}
}

TEST(FastDivideTests, multiplesAndRandomValues) {
	std::mt19937 rng(1);
	for (int32_t divisor = 1; divisor <= 256; divisor++) {
		for (int32_t n = 0; n < 2000; n++) {
			int64_t multiple =
			    static_cast<int64_t>(divisor) * static_cast<int32_t>(rng() % 10000000) * ((rng() & 1) ? 1 : -1);
			for (int64_t x : {multiple - 1, multiple, multiple + 1}) {
				CHECK_EQUAL(static_cast<int32_t>(x) / divisor, divideBySmall(static_cast<int32_t>(x), divisor));
			}
			auto r = static_cast<int32_t>(rng());
			CHECK_EQUAL(r / divisor, divideBySmall(r, divisor));
		}
	}
}
