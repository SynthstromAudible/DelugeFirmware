// Filter morph label classification tests.
// Verifies that SpecificFilter::getFamily() correctly identifies ladder vs SVF modes,
// which determines whether morph params display as "drive"/"FM" or "morph".

#include "CppUTest/TestHarness.h"
#include "model/mod_controllable/filters/filter_config.h"

using deluge::dsp::filter::FilterFamily;
using deluge::dsp::filter::SpecificFilter;

#define CHECK_FAMILY(expected, mode)                                                                                   \
	CHECK_EQUAL(static_cast<int>(expected), static_cast<int>(SpecificFilter(mode).getFamily()))

TEST_GROUP(FilterMorphLabel){};

// LPF ladder modes → "drive" label
TEST(FilterMorphLabel, lpf24dbIsLadder) {
	CHECK_FAMILY(FilterFamily::LP_LADDER, FilterMode::TRANSISTOR_24DB);
}

TEST(FilterMorphLabel, lpf12dbIsLadder) {
	CHECK_FAMILY(FilterFamily::LP_LADDER, FilterMode::TRANSISTOR_12DB);
}

TEST(FilterMorphLabel, lpfDriveIsLadder) {
	CHECK_FAMILY(FilterFamily::LP_LADDER, FilterMode::TRANSISTOR_24DB_DRIVE);
}

// LPF SVF modes → standard "morph" label
TEST(FilterMorphLabel, lpfSvfBandIsSvf) {
	CHECK_FAMILY(FilterFamily::SVF, FilterMode::SVF_BAND);
}

TEST(FilterMorphLabel, lpfSvfNotchIsSvf) {
	CHECK_FAMILY(FilterFamily::SVF, FilterMode::SVF_NOTCH);
}

// HPF ladder mode → "FM" label
TEST(FilterMorphLabel, hpfLadderIsHpLadder) {
	CHECK_FAMILY(FilterFamily::HP_LADDER, FilterMode::HPLADDER);
}

// Verify SVF modes never trigger ladder label paths
TEST(FilterMorphLabel, svfModesNeverMatchLpLadder) {
	FilterMode svfModes[] = {FilterMode::SVF_BAND, FilterMode::SVF_NOTCH};
	for (auto mode : svfModes) {
		CHECK(static_cast<int>(SpecificFilter(mode).getFamily()) != static_cast<int>(FilterFamily::LP_LADDER));
	}
}

TEST(FilterMorphLabel, svfBandDoesNotMatchHpLadder) {
	CHECK(static_cast<int>(SpecificFilter(FilterMode::SVF_BAND).getFamily())
	      != static_cast<int>(FilterFamily::HP_LADDER));
}
