#include "CppUTest/TestHarness.h"
#include "model/sequencing/matriceal_engine.h"

TEST_GROUP(MatricealLaneTest){};

TEST(MatricealLaneTest, forwardWrap) {
	MatricealLane lane;
	lane.length = 3;
	lane.direction = LaneDirection::FORWARD;
	lane.values[0] = 10;
	lane.values[1] = 20;
	lane.values[2] = 30;
	lane.position = 0;

	CHECK_EQUAL(10, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(20, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(30, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(10, lane.currentValue()); // wrapped
	CHECK_EQUAL(0, lane.position);
}

TEST(MatricealLaneTest, reverseWrap) {
	MatricealLane lane;
	lane.length = 3;
	lane.direction = LaneDirection::REVERSE;
	lane.values[0] = 10;
	lane.values[1] = 20;
	lane.values[2] = 30;
	lane.position = 2;

	CHECK_EQUAL(30, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(20, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(10, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(30, lane.currentValue()); // wrapped
}

TEST(MatricealLaneTest, pingpong) {
	MatricealLane lane;
	lane.length = 4;
	lane.direction = LaneDirection::PINGPONG;
	lane.values[0] = 1;
	lane.values[1] = 2;
	lane.values[2] = 3;
	lane.values[3] = 4;
	lane.position = 0;
	lane.pingpongReversed = false;

	// Forward: 1, 2, 3, 4
	CHECK_EQUAL(1, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(2, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(3, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(4, lane.currentValue());
	// Reverse: 3, 2, 1
	lane.advance();
	CHECK_EQUAL(3, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(2, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(1, lane.currentValue());
	// Forward again: 2, 3, 4
	lane.advance();
	CHECK_EQUAL(2, lane.currentValue());
}

TEST(MatricealLaneTest, singleStep) {
	MatricealLane lane;
	lane.length = 1;
	lane.direction = LaneDirection::FORWARD;
	lane.values[0] = 42;
	lane.position = 0;

	CHECK_EQUAL(42, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(42, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(42, lane.currentValue());
}

TEST(MatricealLaneTest, disabledLane) {
	MatricealLane lane;
	lane.length = 0; // disabled

	CHECK_EQUAL(0, lane.currentValue());
	lane.advance(); // should not crash
	CHECK_EQUAL(0, lane.currentValue());
}

TEST(MatricealLaneTest, reset) {
	MatricealLane lane;
	lane.length = 4;
	lane.direction = LaneDirection::FORWARD;
	lane.position = 3;
	lane.pingpongReversed = true;

	lane.reset();
	CHECK_EQUAL(0, lane.position);
	CHECK_EQUAL(false, lane.pingpongReversed);
}
