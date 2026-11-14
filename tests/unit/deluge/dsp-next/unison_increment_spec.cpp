#include "synth/engines/slot_engine.h"

#include "alloc_seams.h"

#include <cppspec.hpp>
#include <cstdint>
#include <optional>

using deluge::synth::detuned_increment;

namespace {
/// The top unison part's detuner at the widest detune: +25 cents.
PhaseIncrementFineTuner widest() {
	PhaseIncrementFineTuner tuner;
	tuner.setup(1073741800);
	return tuner;
}
} // namespace

// clang-format off
describe unison_increment("a unison part's increment", ${
	it("is the base detuned, below 2^31", _ {
		PhaseIncrementFineTuner none;
		expect(detuned_increment(1 << 28, none) == std::optional<uint32_t>{1 << 28}).to_be_true();
		expect(detuned_increment(1 << 30, widest()).value_or(0) > uint32_t{1} << 30).to_be_true();
	});
	it("is none where the detune takes it to 2^31 or above", _ {
		// A 44056 Hz sample transposed up near its limit, at osc +49 and master +49 cents: the widest detune's top part
		// lands at 2174034120, past 2^31.
		expect(detuned_increment(2142841048U, widest()).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_MAIN(unison_increment);
