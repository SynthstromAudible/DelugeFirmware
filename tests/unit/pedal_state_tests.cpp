// PedalState bitfield enum tests.
// Tests the bitwise operators used by Voice to track sustain/sostenuto pedal state.
// The enum is defined in voice.h but copied here to avoid pulling in DSP dependencies.

#include "CppUTest/TestHarness.h"
#include <cstdint>

namespace {

enum class PedalState : uint8_t {
	None = 0,
	SustainDeferred = 1 << 0,
	SostenutoCapture = 1 << 1,
	SostenutoDeferred = 1 << 2
};

constexpr PedalState operator|(PedalState a, PedalState b) {
	return static_cast<PedalState>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr PedalState operator&(PedalState a, PedalState b) {
	return static_cast<PedalState>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr PedalState operator~(PedalState a) {
	return static_cast<PedalState>(~static_cast<uint8_t>(a));
}

bool hasFlag(PedalState state, PedalState flag) {
	return (state & flag) != PedalState::None;
}

} // namespace

TEST_GROUP(PedalStateTest){};

TEST(PedalStateTest, flagValues) {
	CHECK_EQUAL(0, static_cast<uint8_t>(PedalState::None));
	CHECK_EQUAL(1, static_cast<uint8_t>(PedalState::SustainDeferred));
	CHECK_EQUAL(2, static_cast<uint8_t>(PedalState::SostenutoCapture));
	CHECK_EQUAL(4, static_cast<uint8_t>(PedalState::SostenutoDeferred));
}

TEST(PedalStateTest, orCombinesFlags) {
	PedalState combined = PedalState::SustainDeferred | PedalState::SostenutoCapture;
	CHECK_EQUAL(3, static_cast<uint8_t>(combined));
	CHECK_TRUE(hasFlag(combined, PedalState::SustainDeferred));
	CHECK_TRUE(hasFlag(combined, PedalState::SostenutoCapture));
	CHECK_FALSE(hasFlag(combined, PedalState::SostenutoDeferred));
}

TEST(PedalStateTest, clearFlagWithNotAndAnd) {
	PedalState state = PedalState::SustainDeferred | PedalState::SostenutoDeferred;
	PedalState cleared = state & ~PedalState::SustainDeferred;
	CHECK_FALSE(hasFlag(cleared, PedalState::SustainDeferred));
	CHECK_TRUE(hasFlag(cleared, PedalState::SostenutoDeferred));
}

TEST(PedalStateTest, independentFlagsDontOverlap) {
	PedalState result = PedalState::SustainDeferred & PedalState::SostenutoCapture;
	CHECK_EQUAL(0, static_cast<uint8_t>(result));
}

TEST(PedalStateTest, sustainAndSostenutoCombineAndRelease) {
	PedalState state = PedalState::SustainDeferred | PedalState::SostenutoCapture | PedalState::SostenutoDeferred;
	// Release sustain only
	state = state & ~PedalState::SustainDeferred;
	CHECK_FALSE(hasFlag(state, PedalState::SustainDeferred));
	CHECK_TRUE(hasFlag(state, PedalState::SostenutoCapture));
	CHECK_TRUE(hasFlag(state, PedalState::SostenutoDeferred));
	// Release sostenuto
	state = state & ~(PedalState::SostenutoCapture | PedalState::SostenutoDeferred);
	CHECK_EQUAL(0, static_cast<uint8_t>(state));
}

TEST(PedalStateTest, noteOnResetsToNone) {
	PedalState state = PedalState::SustainDeferred | PedalState::SostenutoCapture;
	state = PedalState::None;
	CHECK_EQUAL(0, static_cast<uint8_t>(state));
}
