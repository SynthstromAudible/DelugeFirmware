// Reverb filter encoder round-trip tests.
// Verifies that std::round() prevents float-to-int truncation drift
// when reading back HPF/LPF values in the reverb menu.

#include "CppUTest/TestHarness.h"
#include <cmath>
#include <cstdint>

// The reverb HPF/LPF menu items do:
//   write: filterValue = menuValue / kMaxMenuValue
//   read:  menuValue = round(filterValue * kMaxMenuValue)
// Without round(), float imprecision causes values to drift down by 1.
static constexpr int32_t kMaxMenuValue = 50;

namespace {
struct ReverbFilterSim {
	float filterValue{0.0f};
	void write(int32_t menuValue) { filterValue = static_cast<float>(menuValue) / kMaxMenuValue; }
	int32_t readWithRound() const { return static_cast<int32_t>(std::round(filterValue * kMaxMenuValue)); }
	int32_t readWithTruncation() const { return static_cast<int32_t>(filterValue * kMaxMenuValue); }
};
} // namespace

TEST_GROUP(ReverbFilterRoundtrip) {
	ReverbFilterSim sim;
};

TEST(ReverbFilterRoundtrip, allValuesRoundTripWithRound) {
	for (int32_t v = 0; v <= kMaxMenuValue; v++) {
		sim.write(v);
		CHECK_EQUAL(v, sim.readWithRound());
	}
}

TEST(ReverbFilterRoundtrip, roundNeverDriftsOnRepeatRead) {
	sim.write(25);
	for (int i = 0; i < 100; i++) {
		int32_t readBack = sim.readWithRound();
		CHECK_EQUAL(25, readBack);
		sim.write(readBack);
	}
}
