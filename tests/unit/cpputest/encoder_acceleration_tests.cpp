#include "CppUTest/TestHarness.h"

#include "hid/encoder_acceleration.h"
#include "hid/encoders.h"

#include <array>
#include <span>
#include <vector>

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

TEST_GROUP(ContinuousEncoderTimedRunsTest) {
	deluge::hid::encoders::ContinuousEncoder encoder;

	/// Every turn the encoder hands out since the last call, in order.
	std::vector<int32_t> turns() {
		std::vector<int32_t> out;
		encoder.actionTurns([&](int8_t offset) { out.push_back(offset); });
		return out;
	}

	/// Every turn the encoder hands out since the last call, summed.
	int32_t actioned() {
		int32_t sum = 0;
		encoder.actionTurns([&](int8_t offset) { sum += offset; });
		return sum;
	}
};

// Edges the app takes in one batch, late, accelerate on the times the board decoded them: `main`'s encoder task
// takes each edge of a fast turn alone (`origin/main:src/deluge/hid/encoder_input.cpp`'s `interpretEncoders`), so
// eleven edges 3 ms apart give 1 and then ten at the 3x cap.
TEST(ContinuousEncoderTimedRunsTest, BatchedRunsAccelerateOnTheirOwnTimes) {
	for (int32_t i = 0; i < 11; i++) {
		encoder.applyEdges(1, 1.0 + 0.003 * i);
	}
	LONGS_EQUAL(1 + 10 * 3, actioned());
	LONGS_EQUAL(0, actioned());
}

// A reversal inside one batch starts the speed again from rest, as `main`'s `EncoderAcceleration::advance` does for a
// turn the other way, and then builds it on the reversed runs' own times.
TEST(ContinuousEncoderTimedRunsTest, AReversalMidBatchResetsTheSpeed) {
	encoder.applyEdges(1, 1.000);
	encoder.applyEdges(1, 1.003);
	encoder.applyEdges(1, 1.006);
	encoder.applyEdges(-1, 1.009);
	encoder.applyEdges(-1, 1.012);
	const std::vector<int32_t> want{1, 3, 3, -1, -3};
	CHECK(want == turns());
}

// Runs past the queue's capacity fold into its newest run: no edge is lost, and the folded run goes out at its last
// time, its edges accelerated together.
TEST(ContinuousEncoderTimedRunsTest, RunsPastTheQueueFoldIntoTheNewest) {
	constexpr size_t kRuns = deluge::hid::encoders::ContinuousEncoder::kMaxRuns + 2;
	for (size_t i = 0; i < kRuns; i++) {
		encoder.applyEdges(1, 1.0 + 0.003 * static_cast<double>(i));
	}
	const std::vector<int32_t> got = turns();
	LONGS_EQUAL(deluge::hid::encoders::ContinuousEncoder::kMaxRuns, got.size());
	LONGS_EQUAL(1, got.front());
	LONGS_EQUAL(3 * 3, got.back());
}

// A folded run whose edges add up to nothing is no turn: `main` acts on a gold knob only `if (offset != 0)`, so it
// neither reaches the UI nor moves the acceleration on. The queue is filled, the newest run folds back to 0, and the
// run after it accelerates as though the folded one had not come.
TEST(ContinuousEncoderTimedRunsTest, AFoldThatAddsUpToNothingIsNoTurn) {
	constexpr size_t kRuns = deluge::hid::encoders::ContinuousEncoder::kMaxRuns;
	for (size_t i = 0; i < kRuns; i++) {
		encoder.applyEdges(1, 1.0 + 0.003 * static_cast<double>(i));
	}
	encoder.applyEdges(-1, 2.0); // folds the newest run, +1, to 0
	const std::vector<int32_t> got = turns();
	LONGS_EQUAL(kRuns - 1, got.size());
	// After 0.3 s the speed would reset; the zero run at 2.0 s must not have moved the clock it is measured from.
	encoder.applyEdges(1, 1.0 + 0.003 * static_cast<double>(kRuns));
	LONGS_EQUAL(3, actioned());
}

TEST_GROUP(SplitIntoTurnsTest) {
	/// A run of edges as the board hands it over.
	struct Run {
		int8_t edges;
		uint64_t time_us;
	};
	deluge::hid::encoders::DetentedEncoder encoder;
	std::array<deluge::hid::encoders::TimedDetents, 8> turns{};
};

// Each run that completes a detent is a turn of its own, at that run's time; a run that completes none adds its edges
// to the next. The first click needs three edges, the next ones four.
TEST(SplitIntoTurnsTest, EachRunThatCompletesADetentIsATurnAtItsTime) {
	const std::array<Run, 4> runs{{{2, 100}, {1, 200}, {4, 300}, {2, 400}}};
	const size_t n = deluge::hid::encoders::splitIntoTurns(encoder, std::span<const Run>{runs}, std::span{turns});
	LONGS_EQUAL(2, n);
	LONGS_EQUAL(1, turns[0].detents);
	LONGS_EQUAL(200, turns[0].timeUs);
	LONGS_EQUAL(1, turns[1].detents);
	LONGS_EQUAL(300, turns[1].timeUs);
	CHECK_FALSE(encoder.pending());
}

// Detents already waiting in the encoder, put there without a time, go out first, as one turn with none.
TEST(SplitIntoTurnsTest, DetentsAlreadyWaitingGoOutFirstWithNoTime) {
	encoder.applyEdges(3);
	const std::array<Run, 1> runs{{{4, 500}}};
	const size_t n = deluge::hid::encoders::splitIntoTurns(encoder, std::span<const Run>{runs}, std::span{turns});
	LONGS_EQUAL(2, n);
	LONGS_EQUAL(1, turns[0].detents);
	LONGS_EQUAL(0, turns[0].timeUs);
	LONGS_EQUAL(1, turns[1].detents);
	LONGS_EQUAL(500, turns[1].timeUs);
}
