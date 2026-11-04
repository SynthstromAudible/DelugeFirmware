#include "model/sample/clip_increment.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

namespace {
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
constexpr uint32_t kRate = 44100;
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
	it("place a transpose's interval by its octave, as before, from -96 to +83", _ {
		expect(transposed_neutral(0, kRate, kRate)).to_equal(1 << 24);           // the interval 2^30 >> 6
		expect(transposed_neutral(12, kRate, kRate)).to_equal(1 << 25);
		expect(transposed_neutral(72, kRate, kRate)).to_equal(1 << 30);
		expect(transposed_neutral(-96, kRate, kRate)).to_equal((1 << 30) >> 14);
		expect(transposed_neutral(83, kRate, kRate)).to_equal(noteIntervalTable[11]);
	});
	it("hold the top octaves under 2^31 rather than shifting by a negative count", _ {
		expect(transposed_neutral(84, kRate, kRate)).to_equal(kMax);
		expect(transposed_neutral(96, kRate, kRate)).to_equal(kMax);
	});
	it("scale to the sample's rate, held under 2^31", _ {
		expect(transposed_neutral(0, 22050, kRate)).to_equal(1 << 23);
		// +72 at 96 kHz: 2^30 × 96000 / 44100 is past 2^31
		expect(transposed_neutral(72, 96000, kRate)).to_equal(kMax);
	});
});
// clang-format on

CPPSPEC_MAIN(clip_increment);
