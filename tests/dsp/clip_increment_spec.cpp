#include "model/sample/clip_increment.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

namespace {
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
constexpr uint64_t kOne = uint64_t{1} << 24; ///< A ratio of 1×, Q24.
} // namespace

// clang-format off
describe clip_increment("an audio clip's phase increments", ${
	it("scale by a Q24 ratio, exactly below 2^31", _ {
		expect(scaled_increment(1 << 24, kOne)).to_equal(1 << 24);
		expect(scaled_increment(1 << 30, kOne / 2)).to_equal(1 << 29);
		expect(scaled_increment(3, 3 * kOne)).to_equal(9);
	});
	it("hold under 2^31 past 128× instead of going negative", _ {
		// +72 semitones (2^30, 64×) at double tempo: 2^31
		expect(scaled_increment(1 << 30, 2 * kOne)).to_equal(kMax);
		expect(scaled_increment(static_cast<uint32_t>(kMax), kOne + 1)).to_equal(kMax);
		// a ratio whose product would pass 64 bits
		expect(scaled_increment(1 << 30, uint64_t{1} << 40)).to_equal(kMax);
	});
});
// clang-format on

CPPSPEC_MAIN(clip_increment);
