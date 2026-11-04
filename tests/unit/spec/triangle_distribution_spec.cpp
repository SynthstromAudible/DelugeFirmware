#include "dsp-next/core/triangle.h"

#include "cppspec.hpp"
#include <cstdint>
#include <cstdlib>

namespace {
using deluge::dsp::Q;

/// The triangle of two raw draws.
int32_t triangle_of(int32_t u1, int32_t u2) {
	return deluge::dsp::triangle(Q<31>::from_raw(u1), Q<31>::from_raw(u2)).raw();
}

constexpr int kDraws = 100000;

/// A uniform 32-bit stream, xorshift, from a fixed seed.
struct Uniform {
	uint32_t s = 0x9E3779B9U;
	int32_t next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return static_cast<int32_t>(s);
	}
};

/// The draws within 1/128 of full scale either way: a triangle puts about 1/16384 of its mass there.
int near_the_ends() {
	Uniform u;
	int near = 0;
	for (int i = 0; i < kDraws; ++i) {
		const int32_t u1 = u.next();
		const int32_t u2 = u.next();
		near += std::abs(int64_t{triangle_of(u1, u2)}) >= (int64_t{1} << 31) - (int64_t{1} << 24) ? 1 : 0;
	}
	return near;
}
} // namespace

// clang-format off
describe triangle_distribution("the arp's triangle-distributed random value", ${
	it("reaches its ends about as rarely as a triangle does, not an eighth of the time each", _ {
		// Saturating the two uniform draws' sum would put about 1/4 of the draws there
		expect(near_the_ends() < kDraws / 1000).to_be_true();
	});
	it("is the sum of its two draws' halves, each floored, so it never leaves the range", _ {
		expect(triangle_of(INT32_MAX, INT32_MAX)).to_equal(INT32_MAX - 1);
		expect(triangle_of(INT32_MIN, INT32_MIN)).to_equal(INT32_MIN);
		expect(triangle_of(1 << 30, -(1 << 30))).to_equal(0);
		expect(triangle_of(3, 3)).to_equal(2);
	});
});
// clang-format on

CPPSPEC_SPEC(triangle_distribution)
