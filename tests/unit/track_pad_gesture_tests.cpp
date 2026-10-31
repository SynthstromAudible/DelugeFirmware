#include "CppUTest/TestHarness.h"
#include "gui/views/session/track_pad_gesture.h"

TEST_GROUP(TrackPadGestureTest){};

TEST(TrackPadGestureTest, TapDoesNotAudition) {
	TrackPadGesture gesture;
	gesture.press(100, false);
	CHECK_FALSE(gesture.shouldAudition(199, 100));
}

TEST(TrackPadGestureTest, HoldAuditionsAtThreshold) {
	TrackPadGesture gesture;
	gesture.press(100, false);
	CHECK(gesture.shouldAudition(200, 100));
}

TEST(TrackPadGestureTest, ShiftLockNeverAuditions) {
	TrackPadGesture gesture;
	gesture.press(100, true);
	CHECK_FALSE(gesture.shouldAudition(101, 100));
	CHECK_FALSE(gesture.shouldAudition(10000, 100));
}

TEST(TrackPadGestureTest, AudioTimerWrapDoesNotTurnTapIntoHold) {
	TrackPadGesture gesture;
	gesture.press(UINT32_MAX - 49, false);
	CHECK_FALSE(gesture.shouldAudition(0, 100));
	CHECK(gesture.shouldAudition(50, 100));
}
