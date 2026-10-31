#include "CppUTest/TestHarness.h"
#include "model/track_parameter_edit.h"
#include <limits>

using deluge::model::AutomationRegion;

TEST_GROUP(TrackParameterEdit){};

TEST(TrackParameterEdit, RegionKeepsItsLengthInsideTheTargetLoop) {
	AutomationRegion region{.pos = 24, .length = 48};
	UNSIGNED_LONGS_EQUAL(48, region.lengthWithin(192));
}

TEST(TrackParameterEdit, RegionIsTruncatedAtTheTargetLoopEnd) {
	AutomationRegion region{.pos = 96, .length = 192};
	UNSIGNED_LONGS_EQUAL(96, region.lengthWithin(192));
}

TEST(TrackParameterEdit, RegionDoesNotWrapIntoShorterTargetClips) {
	AutomationRegion region{.pos = 96, .length = 48};
	UNSIGNED_LONGS_EQUAL(0, region.lengthWithin(96));
	UNSIGNED_LONGS_EQUAL(0, region.lengthWithin(48));
}

TEST(TrackParameterEdit, EmptyRegionsAndInvalidLoopsDoNotWrite) {
	AutomationRegion empty{.pos = 24, .length = 0};
	UNSIGNED_LONGS_EQUAL(0, empty.lengthWithin(192));
	AutomationRegion region{.pos = 0, .length = 48};
	UNSIGNED_LONGS_EQUAL(0, region.lengthWithin(0));
	UNSIGNED_LONGS_EQUAL(0, region.lengthWithin(-1));
}

TEST(TrackParameterEdit, ClippingDoesNotOverflowWhenComputingTheRegionEnd) {
	AutomationRegion region{.pos = 24, .length = std::numeric_limits<uint32_t>::max()};
	UNSIGNED_LONGS_EQUAL(168, region.lengthWithin(192));
	region.pos = std::numeric_limits<uint32_t>::max();
	UNSIGNED_LONGS_EQUAL(0, region.lengthWithin(192));
}
