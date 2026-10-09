#include "CppUTest/TestHarness.h"

#include "hid/encoder_acceleration.h"

using deluge::hid::encoders::EncoderAcceleration;
using deluge::hid::encoders::kGoldEncoderAcceleration;
using deluge::hid::encoders::kHorizontalMenuAcceleration;

TEST_GROUP(EncoderAccelerationTest) {
	EncoderAcceleration acceleration;
};

// A movement after idle must remain precise and apply no acceleration.
TEST(EncoderAccelerationTest, FirstMovementUsesUnitMultiplier) {
	DOUBLES_EQUAL(1.0, acceleration.advance(1, 1.0), 0.000001);
}

// Rapid equal movements must follow the acceleration curve and stop at the configured cap.
TEST(EncoderAccelerationTest, RepeatedMovementAcceleratesAndClamps) {
	DOUBLES_EQUAL(1.0, acceleration.advance(1, 1.0), 0.000001);
	DOUBLES_EQUAL(1.5, acceleration.advance(1, 1.01), 0.000001);
	DOUBLES_EQUAL(2.85, acceleration.advance(1, 1.02), 0.000001);
	DOUBLES_EQUAL(3.0, acceleration.advance(1, 1.03), 0.000001);
}

// Reversing direction must discard momentum from the previous direction.
TEST(EncoderAccelerationTest, DirectionChangeResetsAcceleration) {
	acceleration.advance(1, 1.0);
	acceleration.advance(1, 1.01);

	DOUBLES_EQUAL(1.0, acceleration.advance(-1, 1.02), 0.000001);
}

// A differently sized input batch must start a new acceleration run rather than inherit momentum.
TEST(EncoderAccelerationTest, ChangedTickCountInSameDirectionKeepsAccelerating) {
	acceleration.advance(1, 1.0);
	acceleration.advance(1, 1.01);

	DOUBLES_EQUAL(2.85, acceleration.advance(2, 1.02), 0.000001);
}

// A pause at or beyond the reset threshold must return the multiplier to one.
TEST(EncoderAccelerationTest, InactivityResetsAcceleration) {
	acceleration.advance(1, 1.0);
	acceleration.advance(1, 1.01);

	DOUBLES_EQUAL(1.0, acceleration.advance(1, 1.31), 0.000001);
}

// Duplicate or backward timestamps must not divide by zero or produce stale acceleration.
TEST(EncoderAccelerationTest, NonIncreasingTimeDoesNotProduceInvalidAcceleration) {
	acceleration.advance(1, 1.0);
	DOUBLES_EQUAL(1.0, acceleration.advance(1, 1.0), 0.000001);
	DOUBLES_EQUAL(1.0, acceleration.advance(1, 0.9), 0.000001);
}

// Activity on one physical encoder must not accelerate the other encoder.
TEST(EncoderAccelerationTest, EncodersHaveIndependentAccelerationHistory) {
	EncoderAcceleration other;
	acceleration.advance(1, 1.0);
	acceleration.advance(1, 1.01);

	DOUBLES_EQUAL(1.0, other.advance(1, 1.02), 0.000001);
	DOUBLES_EQUAL(2.85, acceleration.advance(1, 1.02), 0.000001);
}

// Gold encoders must retain their 3x cap while horizontal-menu editing retains its 5x cap.
TEST(EncoderAccelerationTest, TuningProfilesPreserveDifferentMaximums) {
	EncoderAcceleration gold;
	EncoderAcceleration horizontalMenu;
	gold.advance(1, 1.0, kGoldEncoderAcceleration);
	horizontalMenu.advance(1, 1.0, kHorizontalMenuAcceleration);

	DOUBLES_EQUAL(3.0, gold.advance(1, 1.001, kGoldEncoderAcceleration), 0.000001);
	DOUBLES_EQUAL(5.0, horizontalMenu.advance(1, 1.001, kHorizontalMenuAcceleration), 0.000001);
}

// The horizontal-menu profile must accelerate below 300 ms and reset above that threshold.
TEST(EncoderAccelerationTest, HorizontalMenuProfileUsesItsConfiguredResetThreshold) {
	acceleration.advance(1, 1.0, kHorizontalMenuAcceleration);
	acceleration.advance(1, 1.01, kHorizontalMenuAcceleration);

	CHECK(acceleration.advance(1, 1.309, kHorizontalMenuAcceleration) > 1.0);
	DOUBLES_EQUAL(1.0, acceleration.advance(1, 1.619, kHorizontalMenuAcceleration), 0.000001);
}

// Changing the selected horizontal-menu item must clear accumulated acceleration explicitly.
TEST(EncoderAccelerationTest, ExplicitResetClearsHorizontalMenuAcceleration) {
	acceleration.advance(1, 1.0, kHorizontalMenuAcceleration);
	acceleration.advance(1, 1.01, kHorizontalMenuAcceleration);
	acceleration.reset();

	DOUBLES_EQUAL(1.0, acceleration.advance(1, 1.02, kHorizontalMenuAcceleration), 0.000001);
}
