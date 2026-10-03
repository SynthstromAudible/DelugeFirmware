#include "CppUTest/TestHarness.h"

#include "hid/encoder_acceleration.h"
#include "hid/encoder_quadrature.h"
#include "hid/encoders.h"

#include <array>

using deluge::hid::encoders::ContinuousEncoder;
using deluge::hid::encoders::DetentedEncoder;
using deluge::hid::encoders::EncoderAcceleration;
using deluge::hid::encoders::kGoldEncoderAcceleration;
using deluge::hid::encoders::kHorizontalMenuAcceleration;
using deluge::hid::encoders::QuadratureDecoder;

namespace {

struct EncoderPins {
	bool a;
	bool b;
};

template <size_t N>
int8_t readGoldEncoderMovement(const std::array<EncoderPins, N>& states, bool pollB, bool invert = false) {
	ContinuousEncoder encoder;
	QuadratureDecoder decoder;
	decoder.initialize(states[0].a, states[0].b);
	for (size_t i = 1; i < states.size(); ++i) {
		if (states[i].a != states[i - 1].a) {
			encoder.apply_edges(decoder.observeAEdge(states[i].a, states[i].b, invert));
		}
		else if (pollB) {
			encoder.apply_edges(decoder.observe(states[i].a, states[i].b, invert));
		}
	}
	return encoder.take();
}

template <size_t N>
int32_t readBlackEncoderMovement(const std::array<EncoderPins, N>& states, bool pollB, bool invert = false) {
	DetentedEncoder encoder;
	QuadratureDecoder decoder;
	decoder.initialize(states[0].a, states[0].b);
	for (size_t i = 1; i < states.size(); ++i) {
		if (states[i].a != states[i - 1].a) {
			encoder.apply_edges(decoder.observeAEdge(states[i].a, states[i].b, invert));
		}
		else if (pollB) {
			encoder.apply_edges(decoder.observe(states[i].a, states[i].b, invert));
		}
	}
	return encoder.take();
}

} // namespace

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

TEST_GROUP(GoldEncoderQuadratureTest){};

// A fully observed positive cycle must confirm every edge except the last, which waits for the next edge.
TEST(GoldEncoderQuadratureTest, CompletePositiveCycleConfirmsAllButLastEdge) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(3, readGoldEncoderMovement(states, true));
}

// A fully observed reverse cycle must confirm the same number of edges with negative polarity.
TEST(GoldEncoderQuadratureTest, CompleteNegativeCycleConfirmsAllButLastEdge) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{true, false}, EncoderPins{true, true},
	                               EncoderPins{false, true}, EncoderPins{false, false}};

	CHECK_EQUAL(-3, readGoldEncoderMovement(states, true));
}

// Continuing into the next cycle must confirm the final edge, giving four edges per cycle.
TEST(GoldEncoderQuadratureTest, NextEdgeConfirmsFourthEdgeOfCycle) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true},  EncoderPins{true, true},
	                               EncoderPins{true, false},  EncoderPins{false, false}, EncoderPins{false, true}};

	CHECK_EQUAL(4, readGoldEncoderMovement(states, true));
}

// The per-encoder wiring inversion flag must reverse the decoded direction.
TEST(GoldEncoderQuadratureTest, InvertedWiringReversesDirection) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(-3, readGoldEncoderMovement(states, true, true));
}

// A slow B-only transition, which cannot generate an A-pin interrupt, must be confirmed by a later poll.
TEST(GoldEncoderQuadratureTest, PollConfirmsSlowBTransition) {
	QuadratureDecoder decoder;
	decoder.initialize(false, false);

	CHECK_EQUAL(0, decoder.observe(false, true));
	CHECK_EQUAL(1, decoder.observe(true, true));
}

// A single edge must not report movement until the other pin confirms it.
TEST(GoldEncoderQuadratureTest, SingleEdgeIsNotReportedUntilConfirmed) {
	QuadratureDecoder decoder;
	decoder.initialize(false, false);

	CHECK_EQUAL(0, decoder.observeAEdge(true, false));
	CHECK_EQUAL(-1, decoder.observe(true, true));
}

// B flickering around one transition must not report movement.
TEST(GoldEncoderQuadratureTest, BPinChatterDoesNotReportMovement) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{false, false},
	                               EncoderPins{false, true}, EncoderPins{false, false}};

	CHECK_EQUAL(0, readGoldEncoderMovement(states, true));
}

// A flickering around one transition must not report movement, with or without intermediate polls.
TEST(GoldEncoderQuadratureTest, APinChatterDoesNotReportMovement) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{true, false}, EncoderPins{false, false},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(0, readGoldEncoderMovement(states, true));
	CHECK_EQUAL(0, readGoldEncoderMovement(states, false));
}

// A pin flickering after real movement must not add or remove confirmed edges.
TEST(GoldEncoderQuadratureTest, ChatterAfterMovementDoesNotChangeCount) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{false, true},  EncoderPins{true, true},  EncoderPins{false, true},
	                               EncoderPins{true, true}};

	CHECK_EQUAL(1, readGoldEncoderMovement(states, true));
}

// Turning back to the start must net to zero once the reversal is confirmed.
TEST(GoldEncoderQuadratureTest, ReversalNetsToZeroOnceConfirmed) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true},  EncoderPins{true, true},
	                               EncoderPins{false, true},  EncoderPins{false, false}, EncoderPins{true, false}};

	CHECK_EQUAL(0, readGoldEncoderMovement(states, true));
}

// A delayed A interrupt must replay the unobserved preceding B transition, confirming it.
TEST(GoldEncoderQuadratureTest, DelayedAInterruptRecoversUnpolledBEdge) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true}};

	CHECK_EQUAL(1, readGoldEncoderMovement(states, false));
}

// A interrupts alone must decode a fast cycle exactly as if every transition had been polled.
TEST(GoldEncoderQuadratureTest, FastCompleteCycleMatchesPolledCycle) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(readGoldEncoderMovement(states, true), readGoldEncoderMovement(states, false));
}

// A poll that sees both bits changed must leave the movement for the pending A interrupt to resolve.
TEST(GoldEncoderQuadratureTest, AmbiguousPollLeavesMovementForPendingAInterrupt) {
	QuadratureDecoder decoder;
	decoder.initialize(false, false);

	CHECK_EQUAL(0, decoder.observe(true, true));
	CHECK_EQUAL(1, decoder.observeAEdge(true, true));
	CHECK_EQUAL(1, decoder.observe(true, false));
}

TEST_GROUP(BlackEncoderQuadratureTest){};

// Fewer than four quadrature transitions must remain buffered and produce no black-encoder detent.
TEST(BlackEncoderQuadratureTest, PartialCycleDoesNotReportDetent) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true}};

	CHECK_EQUAL(0, readBlackEncoderMovement(states, true));
}

// Four positive quadrature transitions must collapse to exactly one black-encoder detent.
TEST(BlackEncoderQuadratureTest, CompleteCycleReportsOneDetent) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(1, readBlackEncoderMovement(states, true));
}

// Four reverse quadrature transitions must collapse to exactly one negative detent.
TEST(BlackEncoderQuadratureTest, ReverseCycleReportsNegativeDetent) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{true, false}, EncoderPins{true, true},
	                               EncoderPins{false, true}, EncoderPins{false, false}};

	CHECK_EQUAL(-1, readBlackEncoderMovement(states, true));
}

// A fast cycle reconstructed from A interrupts must still produce exactly one detent.
TEST(BlackEncoderQuadratureTest, FastCycleReportsOneDetentWithoutIntermediatePolls) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(1, readBlackEncoderMovement(states, false));
}

// Consecutive complete cycles must accumulate without dropping or merging detents.
TEST(BlackEncoderQuadratureTest, TwoCyclesReportTwoDetents) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true},  EncoderPins{true, true},
	                               EncoderPins{true, false},  EncoderPins{false, false}, EncoderPins{false, true},
	                               EncoderPins{true, true},   EncoderPins{true, false},  EncoderPins{false, false}};

	CHECK_EQUAL(2, readBlackEncoderMovement(states, true));
}

// A partial cycle must survive a task drain and combine with later edges into one detent.
TEST(BlackEncoderQuadratureTest, PartialCyclePersistsAcrossDrains) {
	DetentedEncoder encoder;
	encoder.apply_edges(2);
	CHECK_EQUAL(0, encoder.take());
	encoder.apply_edges(2);

	CHECK_EQUAL(1, encoder.take());
}

// Reversing before a detent completes must cancel buffered edges without producing movement.
TEST(BlackEncoderQuadratureTest, ReversalCancelsPartialCycle) {
	DetentedEncoder encoder;
	encoder.apply_edges(2);
	CHECK_EQUAL(0, encoder.take());
	encoder.apply_edges(-2);

	CHECK_EQUAL(0, encoder.take());
}

// A single accumulated batch must preserve every complete positive or negative detent.
TEST(BlackEncoderQuadratureTest, BatchedEdgesProduceAllDetents) {
	DetentedEncoder encoder;
	encoder.apply_edges(12);
	CHECK_EQUAL(3, encoder.take());
	encoder.apply_edges(-12);

	CHECK_EQUAL(-3, encoder.take());
}

// Restoring deferred movement must add to, rather than overwrite, movement that arrives afterward.
TEST(BlackEncoderQuadratureTest, RestoredDetentCombinesWithNewMovement) {
	DetentedEncoder encoder;
	encoder.restore(1);
	encoder.apply_edges(4);

	CHECK_EQUAL(2, encoder.take());
}

// Mixing a polled half-cycle with an unpolled half-cycle must still total exactly one detent.
TEST(BlackEncoderQuadratureTest, MixedPollingAndInterruptRecoveryProducesOneDetent) {
	DetentedEncoder encoder;
	QuadratureDecoder decoder;
	decoder.initialize(false, false);
	encoder.apply_edges(decoder.observe(false, true));
	encoder.apply_edges(decoder.observeAEdge(true, true));
	encoder.apply_edges(decoder.observeAEdge(false, false));

	CHECK_EQUAL(1, encoder.take());
}

// Inverted black-encoder wiring must reverse the detent while preserving its magnitude.
TEST(BlackEncoderQuadratureTest, InvertedWiringReversesDetentDirection) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true}, EncoderPins{true, true},
	                               EncoderPins{true, false}, EncoderPins{false, false}};

	CHECK_EQUAL(-1, readBlackEncoderMovement(states, true, true));
}

// A pin flickering while the knob rests in a detent must not produce extra detents.
TEST(BlackEncoderQuadratureTest, ChatterAtRestDoesNotReportExtraDetents) {
	constexpr std::array states = {EncoderPins{false, false}, EncoderPins{false, true},  EncoderPins{true, true},
	                               EncoderPins{true, false},  EncoderPins{false, false}, EncoderPins{true, false},
	                               EncoderPins{false, false}, EncoderPins{true, false},  EncoderPins{false, false}};

	CHECK_EQUAL(1, readBlackEncoderMovement(states, true));
	CHECK_EQUAL(1, readBlackEncoderMovement(states, false));
}
