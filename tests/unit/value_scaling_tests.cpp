#include "CppUTest/TestHarness.h"
#include "gui/menu_item/value_scaling.h"
#include "modulation/arpeggiator.h"

#include "definitions_cxx.hpp"

TEST_GROUP(ValueScalingTest) {};

TEST(ValueScalingTest, unpatchedParamTest) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForUnpatched(computeFinalValueForUnpatched(i)));
	}
}

TEST(ValueScalingTest, panTest) {
	for (int i = kMinMenuRelativeValue; i <= kMaxMenuRelativeValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForPan(computeFinalValueForPan(i)));
	}
}

TEST(ValueScalingTest, compressorTest) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForCompressor(computeFinalValueForCompressor(i)));
	}
}

TEST(ValueScalingTest, integerTest) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForInteger(computeFinalValueForInteger(i)));
	}
}

TEST(ValueScalingTest, arpGateTest) {
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForArpGate(computeFinalValueForArpGate(i)));
	}
}

TEST(ValueScalingTest, arpRatchetTest) {
	return; // TODO: failing test, fix.
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForArpRatchet(computeFinalValueForArpRatchet(i)));
	}
}

TEST(ValueScalingTest, arpRatchetProbabilityTest) {
	return; // TODO: failing test, fix.
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForArpRatchetProbability(computeFinalValueForArpRatchetProbability(i)));
	}
}

TEST(ValueScalingTest, arpRhythmTest) {
	return; // TODO: failing test, fix.
	for (int i = 0; i <= NUM_PRESET_ARP_RHYTHMS - 1; i++) {
		CHECK_EQUAL(i, computeCurrentValueForArpRhythm(computeFinalValueForArpRhythm(i)));
	}
}

TEST(ValueScalingTest, arpSeqLengthTest) {
	return; // TODO: failing test, fix.
	for (int i = kMinMenuValue; i <= kMaxMenuValue; i++) {
		CHECK_EQUAL(i, computeCurrentValueForArpSeqLength(computeFinalValueForArpSeqLength(i)));
	}
}
