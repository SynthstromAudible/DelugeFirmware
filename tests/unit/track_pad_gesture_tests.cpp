#include "CppUTest/TestHarness.h"
#include "gui/views/session/track_pad_gesture.h"

TEST_GROUP(TrackPadGestureTest){};

TEST(TrackPadGestureTest, PlainPressIsNotSilent) {
	TrackPadGesture gesture;
	gesture.press(false);
	CHECK_FALSE(gesture.silent());
}

TEST(TrackPadGestureTest, ShiftPressStaysSilent) {
	TrackPadGesture gesture;
	gesture.press(true);
	CHECK(gesture.silent());
}

TEST(TrackPadGestureTest, NextPressReplacesShiftState) {
	TrackPadGesture gesture;
	gesture.press(true);
	gesture.press(false);
	CHECK_FALSE(gesture.silent());
}
