#include "dsp/fixed/pitch/fine_tune.h"
#include "support/oracles/phase_increment_fine_tuner.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstdint>

#include "support/spec_main.h"

using deluge::dsp::fixed::FineTune;
using deluge::test::oracle::PhaseIncrementFineTuner;

namespace {

/// Increments across the range a fine tune sees: none, the smallest, mid-range, and the largest below 2^31.
constexpr std::array<int32_t, 6> kIncrements{0, 1, 1 << 20, 1 << 28, 1 << 30, 2147483647};

/// Whether @p tune offsets every increment as @p oracle (the app's tuner, the arithmetic this one carries) does.
bool agrees(const FineTune& tune, const PhaseIncrementFineTuner& oracle) {
	return std::ranges::all_of(kIncrements,
	                           [&](int32_t increment) { return tune.apply(increment) == oracle.detune(increment); });
}

/// Whether the tune of step @p step agrees with the oracle set up with the same step.
bool agrees_at_step(int32_t step) {
	PhaseIncrementFineTuner oracle;
	oracle.setup(step);
	return agrees(FineTune::of_step(step), oracle);
}

} // namespace

// clang-format off
describe fine_tune("a fine tune of a phase increment", ${
	it("is no offset by default", _ {
		PhaseIncrementFineTuner none;
		none.setNoDetune();
		expect(agrees(FineTune{}, none)).to_equal(true);
		expect(FineTune{}.apply(1 << 28)).to_equal(1 << 28);
	});

	it("offsets as the app's tuner does at every cent, held to a quarter tone", _ {
		for (int32_t cents = -60; cents <= 60; cents++) {
			PhaseIncrementFineTuner oracle;
			oracle.setup_cents(cents);
			expect(agrees(FineTune::of_cents(cents), oracle)).to_equal(true);
		}
		expect(FineTune::of_cents(60) == FineTune::of_cents(50)).to_equal(true);
		expect(FineTune::step_of(-60)).to_equal(PhaseIncrementFineTuner::cents_step(-60));
	});

	it("offsets as the app's tuner does at any step", _ {
		for (const int32_t step : {-1073741824, -1, 0, 1, 1073741823, 2147483647}) {
			expect(agrees_at_step(step)).to_equal(true);
		}
		uint32_t seed = 12345;
		for (int i = 0; i < 1000; i++) {
			seed = seed * 1664525U + 1013904223U; // a fixed LCG: the same steps on every run
			expect(agrees_at_step(static_cast<int32_t>(seed))).to_equal(true);
		}
	});
});
// clang-format on

DELUGE_SPEC(fine_tune_spec, fine_tune)
