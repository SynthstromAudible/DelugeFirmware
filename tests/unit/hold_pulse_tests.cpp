#include "CppUTest/TestHarness.h"
#include "gui/views/hold_pulse.h"

TEST_GROUP(HoldPulseTest){};

TEST(HoldPulseTest, TapNeverPulses) {
	HoldPulse pulse;
	auto* output = reinterpret_cast<Output*>(0x1000);
	CHECK_FALSE(pulse.update(output, 100, 100));
	CHECK(pulse.waiting());
	CHECK_FALSE(pulse.update(nullptr, 150, 100));
	POINTERS_EQUAL(nullptr, pulse.pulsingFor(nullptr));
	CHECK_FALSE(pulse.tracking());
}

TEST(HoldPulseTest, HoldPulsesAtThresholdAndStopsOnRelease) {
	HoldPulse pulse;
	auto* output = reinterpret_cast<Output*>(0x1000);
	pulse.update(output, 100, 100);
	CHECK_FALSE(pulse.update(output, 199, 100));
	CHECK(pulse.update(output, 200, 100));
	POINTERS_EQUAL(output, pulse.pulsingFor(output));
	CHECK(pulse.update(nullptr, 300, 100));
	POINTERS_EQUAL(nullptr, pulse.pulsingFor(nullptr));
}

TEST(HoldPulseTest, SwitchingPadsRestartsTheHold) {
	HoldPulse pulse;
	auto* first = reinterpret_cast<Output*>(0x1000);
	auto* second = reinterpret_cast<Output*>(0x2000);
	pulse.update(first, 100, 100);
	pulse.update(first, 200, 100);
	CHECK(pulse.update(second, 250, 100));
	POINTERS_EQUAL(nullptr, pulse.pulsingFor(second));
	CHECK(pulse.update(second, 350, 100));
	POINTERS_EQUAL(second, pulse.pulsingFor(second));
}

TEST(HoldPulseTest, StaleStateNeverPulsesAnotherCandidate) {
	HoldPulse pulse;
	auto* output = reinterpret_cast<Output*>(0x1000);
	pulse.update(output, 100, 100);
	pulse.update(output, 200, 100);
	POINTERS_EQUAL(nullptr, pulse.pulsingFor(nullptr));
}

TEST(HoldPulseTest, AudioTimerWrapDoesNotTurnTapIntoHold) {
	HoldPulse pulse;
	auto* output = reinterpret_cast<Output*>(0x1000);
	pulse.update(output, UINT32_MAX - 49, 100);
	CHECK_FALSE(pulse.update(output, 0, 100));
	CHECK(pulse.update(output, 50, 100));
}

TEST(HoldPulseTest, RemainingCountsDownToTheHold) {
	HoldPulse pulse;
	auto* output = reinterpret_cast<Output*>(0x1000);
	CHECK_EQUAL(0u, pulse.remaining(100, 100));
	pulse.update(output, 100, 100);
	CHECK_EQUAL(100u, pulse.remaining(100, 100));
	CHECK_EQUAL(1u, pulse.remaining(199, 100));
	CHECK_EQUAL(0u, pulse.remaining(200, 100));
	pulse.update(output, 200, 100);
	CHECK_EQUAL(0u, pulse.remaining(200, 100));
}

TEST(HoldPulseTest, RemainingSurvivesAudioTimerWrap) {
	HoldPulse pulse;
	auto* output = reinterpret_cast<Output*>(0x1000);
	pulse.update(output, UINT32_MAX - 49, 100);
	CHECK_EQUAL(50u, pulse.remaining(0, 100));
}
