#include "CppUTest/TestHarness.h"
#include "model/clip/step_record.h"

TEST_GROUP(StepRecord){};

TEST(StepRecord, advanceWithinBounds) {
	CHECK_EQUAL(4, stepRecordAdvancePosition(0, 4, 16));
	CHECK_EQUAL(8, stepRecordAdvancePosition(4, 4, 16));
	CHECK_EQUAL(15, stepRecordAdvancePosition(15, 0, 16));
}

TEST(StepRecord, advanceWrapsAtClipEnd) {
	CHECK_EQUAL(0, stepRecordAdvancePosition(12, 4, 16));
	CHECK_EQUAL(1, stepRecordAdvancePosition(9, 8, 16));
	CHECK_EQUAL(12, stepRecordAdvancePosition(8, 4, 16));
}

TEST(StepRecord, advanceLandingExactlyOnEndWrapsToStart) {
	CHECK_EQUAL(0, stepRecordAdvancePosition(12, 4, 16));
	CHECK_EQUAL(0, stepRecordAdvancePosition(13, 3, 16));
}

TEST(StepRecord, advanceLargerThanLoopWrapsMoreThanOnce) {
	CHECK_EQUAL(2, stepRecordAdvancePosition(14, 20, 16));
	CHECK_EQUAL(1, stepRecordAdvancePosition(3, 30, 16));
}

TEST(StepRecord, degenerateLengthLeavesPositionAlone) {
	CHECK_EQUAL(3, stepRecordAdvancePosition(3, 4, 0));
	CHECK_EQUAL(3, stepRecordAdvancePosition(3, 4, -1));
}
