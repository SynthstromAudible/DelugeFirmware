#include "synth/engines/ring_mod_engine.h"

#include "alloc_seams.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using deluge::synth::RingModEngine;

namespace {
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
constexpr int32_t kUnityGain = 1 << 28; ///< The filters' gain at unity, Q28.
} // namespace

// clang-format off
describe ring_mod_level("the ring mod's level", ${
	it("is 2^27 for sines, doubled for a saw and quadrupled for a supersaw or wavetable", _ {
		expect(RingModEngine::level(false, 0, OscType::SINE, OscType::SINE)).to_equal(1 << 27);
		expect(RingModEngine::level(false, 0, OscType::SAW, OscType::SINE)).to_equal(1 << 28);
		expect(RingModEngine::level(false, 0, OscType::SINE, OscType::SUPERSAW)).to_equal(1 << 29);
	});
	it("takes the filters' gain, rounded to Q27", _ {
		expect(RingModEngine::level(true, kUnityGain, OscType::SAW, OscType::SINE)).to_equal(1 << 28);
	});
	it("holds at full scale rather than wrapping where the shifts pass it", _ {
		// Two supersaws: 2^27 × 16 is 2^31, unfiltered
		expect(RingModEngine::level(false, 0, OscType::SUPERSAW, OscType::SUPERSAW)).to_equal(kMax);
		// and through a resonant ladder's gain compensation, 1.2 at its top
		expect(RingModEngine::level(true, 322500000, OscType::SUPERSAW, OscType::WAVETABLE)).to_equal(kMax);
		expect(RingModEngine::level(true, kUnityGain, OscType::SUPERSAW, OscType::SUPERSAW)).to_equal(kMax);
	});
});
// clang-format on

CPPSPEC_MAIN(ring_mod_level);
