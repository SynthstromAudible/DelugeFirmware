#include "hid/encoder_acceleration.h"
#include "hid/encoders.h"

#include <array>
#include <span>
#include <vector>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::hid::encoders::EncoderAcceleration;
using deluge::hid::encoders::kGoldEncoderAcceleration;
using deluge::hid::encoders::kHorizontalMenuAcceleration;

namespace {

struct EncoderAccelerationTest {
	EncoderAcceleration acceleration;

	// A movement after idle must remain precise and apply no acceleration.
	void firstMovementUsesUnitMultiplier(auto& self) {
		expect(acceleration.advance(1, 1.0)).to_be_within(0.000001).of(1.0);
	}

	// Rapid equal movements must follow the acceleration curve and stop at the configured cap.
	void repeatedMovementAcceleratesAndClamps(auto& self) {
		expect(acceleration.advance(1, 1.0)).to_be_within(0.000001).of(1.0);
		expect(acceleration.advance(1, 1.01)).to_be_within(0.000001).of(1.5);
		expect(acceleration.advance(1, 1.02)).to_be_within(0.000001).of(2.85);
		expect(acceleration.advance(1, 1.03)).to_be_within(0.000001).of(3.0);
	}

	// Reversing direction must discard momentum from the previous direction.
	void directionChangeResetsAcceleration(auto& self) {
		acceleration.advance(1, 1.0);
		acceleration.advance(1, 1.01);

		expect(acceleration.advance(-1, 1.02)).to_be_within(0.000001).of(1.0);
	}

	// A differently sized input batch must start a new acceleration run rather than inherit momentum.
	void changedTickCountInSameDirectionKeepsAccelerating(auto& self) {
		acceleration.advance(1, 1.0);
		acceleration.advance(1, 1.01);

		expect(acceleration.advance(2, 1.02)).to_be_within(0.000001).of(2.85);
	}

	// A pause at or beyond the reset threshold must return the multiplier to one.
	void inactivityResetsAcceleration(auto& self) {
		acceleration.advance(1, 1.0);
		acceleration.advance(1, 1.01);

		expect(acceleration.advance(1, 1.31)).to_be_within(0.000001).of(1.0);
	}

	// Duplicate or backward timestamps must not divide by zero or produce stale acceleration.
	void nonIncreasingTimeDoesNotProduceInvalidAcceleration(auto& self) {
		acceleration.advance(1, 1.0);
		expect(acceleration.advance(1, 1.0)).to_be_within(0.000001).of(1.0);
		expect(acceleration.advance(1, 0.9)).to_be_within(0.000001).of(1.0);
	}

	// Activity on one physical encoder must not accelerate the other encoder.
	void encodersHaveIndependentAccelerationHistory(auto& self) {
		EncoderAcceleration other;
		acceleration.advance(1, 1.0);
		acceleration.advance(1, 1.01);

		expect(other.advance(1, 1.02)).to_be_within(0.000001).of(1.0);
		expect(acceleration.advance(1, 1.02)).to_be_within(0.000001).of(2.85);
	}

	// Gold encoders must retain their 3x cap while horizontal-menu editing retains its 5x cap.
	void tuningProfilesPreserveDifferentMaximums(auto& self) {
		EncoderAcceleration gold;
		EncoderAcceleration horizontalMenu;
		gold.advance(1, 1.0, kGoldEncoderAcceleration);
		horizontalMenu.advance(1, 1.0, kHorizontalMenuAcceleration);

		expect(gold.advance(1, 1.001, kGoldEncoderAcceleration)).to_be_within(0.000001).of(3.0);
		expect(horizontalMenu.advance(1, 1.001, kHorizontalMenuAcceleration)).to_be_within(0.000001).of(5.0);
	}

	// The horizontal-menu profile must accelerate below 300 ms and reset above that threshold.
	void horizontalMenuProfileUsesItsConfiguredResetThreshold(auto& self) {
		acceleration.advance(1, 1.0, kHorizontalMenuAcceleration);
		acceleration.advance(1, 1.01, kHorizontalMenuAcceleration);

		expect(acceleration.advance(1, 1.309, kHorizontalMenuAcceleration)).to_be_greater_than(1.0);
		expect(acceleration.advance(1, 1.619, kHorizontalMenuAcceleration)).to_be_within(0.000001).of(1.0);
	}

	// Changing the selected horizontal-menu item must clear accumulated acceleration explicitly.
	void explicitResetClearsHorizontalMenuAcceleration(auto& self) {
		acceleration.advance(1, 1.0, kHorizontalMenuAcceleration);
		acceleration.advance(1, 1.01, kHorizontalMenuAcceleration);
		acceleration.reset();

		expect(acceleration.advance(1, 1.02, kHorizontalMenuAcceleration)).to_be_within(0.000001).of(1.0);
	}
};

struct ContinuousEncoderTimedRunsTest {
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

	// Edges the app takes in one batch, late, accelerate on the times the board decoded them: `main`'s encoder task
	// takes each edge of a fast turn alone (`origin/main:src/deluge/hid/encoder_input.cpp`'s `interpretEncoders`), so
	// eleven edges 3 ms apart give 1 and then ten at the 3x cap.
	void batchedRunsAccelerateOnTheirOwnTimes(auto& self) {
		for (int32_t i = 0; i < 11; i++) {
			encoder.applyEdges(1, 1.0 + 0.003 * i);
		}
		expect(actioned()).to_equal(1 + 10 * 3);
		expect(actioned()).to_equal(0);
	}

	// A reversal inside one batch starts the speed again from rest, as `main`'s `EncoderAcceleration::advance` does for
	// a turn the other way, and then builds it on the reversed runs' own times.
	void aReversalMidBatchResetsTheSpeed(auto& self) {
		encoder.applyEdges(1, 1.000);
		encoder.applyEdges(1, 1.003);
		encoder.applyEdges(1, 1.006);
		encoder.applyEdges(-1, 1.009);
		encoder.applyEdges(-1, 1.012);
		const std::vector<int32_t> want{1, 3, 3, -1, -3};
		expect(turns()).to_equal(want);
	}

	// Runs past the queue's capacity fold into its newest run: no edge is lost, and the folded run goes out at its last
	// time, its edges accelerated together.
	void runsPastTheQueueFoldIntoTheNewest(auto& self) {
		constexpr size_t kRuns = deluge::hid::encoders::ContinuousEncoder::kMaxRuns + 2;
		for (size_t i = 0; i < kRuns; i++) {
			encoder.applyEdges(1, 1.0 + 0.003 * static_cast<double>(i));
		}
		const std::vector<int32_t> got = turns();
		expect(got.size()).to_equal(deluge::hid::encoders::ContinuousEncoder::kMaxRuns);
		expect(got.front()).to_equal(1);
		expect(got.back()).to_equal(3 * 3);
	}

	// A folded run whose edges add up to nothing is no turn: `main` acts on a gold knob only `if (offset != 0)`, so it
	// neither reaches the UI nor moves the acceleration on. The queue is filled, the newest run folds back to 0, and
	// the run after it accelerates as though the folded one had not come.
	void aFoldThatAddsUpToNothingIsNoTurn(auto& self) {
		constexpr size_t kRuns = deluge::hid::encoders::ContinuousEncoder::kMaxRuns;
		for (size_t i = 0; i < kRuns; i++) {
			encoder.applyEdges(1, 1.0 + 0.003 * static_cast<double>(i));
		}
		encoder.applyEdges(-1, 2.0); // folds the newest run, +1, to 0
		const std::vector<int32_t> got = turns();
		expect(got.size()).to_equal(kRuns - 1);
		// After 0.3 s the speed would reset; the zero run at 2.0 s must not have moved the clock it is measured from.
		encoder.applyEdges(1, 1.0 + 0.003 * static_cast<double>(kRuns));
		expect(actioned()).to_equal(3);
	}
};

struct SplitIntoTurnsTest {
	/// A run of edges as the board hands it over.
	struct Run {
		int8_t edges;
		uint64_t time_us;
	};
	deluge::hid::encoders::DetentedEncoder encoder;
	std::array<deluge::hid::encoders::TimedDetents, 8> turns{};

	// Each run that completes a detent is a turn of its own, at that run's time; a run that completes none adds its
	// edges to the next. The first click needs three edges, the next ones four.
	void eachRunThatCompletesADetentIsATurnAtItsTime(auto& self) {
		const std::array<Run, 4> runs{{{2, 100}, {1, 200}, {4, 300}, {2, 400}}};
		const size_t n = deluge::hid::encoders::splitIntoTurns(encoder, std::span<const Run>{runs}, std::span{turns});
		expect(n).to_equal(2);
		expect(turns[0].detents).to_equal(1);
		expect(turns[0].timeUs).to_equal(200);
		expect(turns[1].detents).to_equal(1);
		expect(turns[1].timeUs).to_equal(300);
		expect(encoder.pending()).to_be_false();
	}

	// Detents already waiting in the encoder, put there without a time, go out first, as one turn with none.
	void detentsAlreadyWaitingGoOutFirstWithNoTime(auto& self) {
		encoder.applyEdges(3);
		const std::array<Run, 1> runs{{{4, 500}}};
		const size_t n = deluge::hid::encoders::splitIntoTurns(encoder, std::span<const Run>{runs}, std::span{turns});
		expect(n).to_equal(2);
		expect(turns[0].detents).to_equal(1);
		expect(turns[0].timeUs).to_equal(0);
		expect(turns[1].detents).to_equal(1);
		expect(turns[1].timeUs).to_equal(500);
	}
};
} // namespace

// clang-format off
describe encoder_acceleration_group("encoder acceleration", ${
	it("first movement uses unit multiplier", _ { EncoderAccelerationTest{}.firstMovementUsesUnitMultiplier(self); });

	it("repeated movement accelerates and clamps", _ { EncoderAccelerationTest{}.repeatedMovementAcceleratesAndClamps(self); });

	it("direction change resets acceleration", _ { EncoderAccelerationTest{}.directionChangeResetsAcceleration(self); });

	it("changed tick count in same direction keeps accelerating", _ { EncoderAccelerationTest{}.changedTickCountInSameDirectionKeepsAccelerating(self); });

	it("inactivity resets acceleration", _ { EncoderAccelerationTest{}.inactivityResetsAcceleration(self); });

	it("non-increasing time does not produce invalid acceleration", _ { EncoderAccelerationTest{}.nonIncreasingTimeDoesNotProduceInvalidAcceleration(self); });

	it("encoders have independent acceleration history", _ { EncoderAccelerationTest{}.encodersHaveIndependentAccelerationHistory(self); });

	it("tuning profiles preserve different maximums", _ { EncoderAccelerationTest{}.tuningProfilesPreserveDifferentMaximums(self); });

	it("horizontal menu profile uses its configured reset threshold", _ { EncoderAccelerationTest{}.horizontalMenuProfileUsesItsConfiguredResetThreshold(self); });

	it("explicit reset clears horizontal menu acceleration", _ { EncoderAccelerationTest{}.explicitResetClearsHorizontalMenuAcceleration(self); });
});

describe continuous_encoder_timed_runs_group("continuous encoder timed runs", ${
	it("batched runs accelerate on their own times", _ { ContinuousEncoderTimedRunsTest{}.batchedRunsAccelerateOnTheirOwnTimes(self); });

	it("a reversal mid-batch resets the speed", _ { ContinuousEncoderTimedRunsTest{}.aReversalMidBatchResetsTheSpeed(self); });

	it("runs past the queue fold into the newest", _ { ContinuousEncoderTimedRunsTest{}.runsPastTheQueueFoldIntoTheNewest(self); });

	it("a fold that adds up to nothing is no turn", _ { ContinuousEncoderTimedRunsTest{}.aFoldThatAddsUpToNothingIsNoTurn(self); });
});

describe split_into_turns_group("split into turns", ${
	it("each run that completes a detent is a turn at its time", _ { SplitIntoTurnsTest{}.eachRunThatCompletesADetentIsATurnAtItsTime(self); });

	it("detents already waiting go out first with no time", _ { SplitIntoTurnsTest{}.detentsAlreadyWaitingGoOutFirstWithNoTime(self); });
});

DELUGE_SPEC(encoder_acceleration_spec, encoder_acceleration_group, continuous_encoder_timed_runs_group, split_into_turns_group)
