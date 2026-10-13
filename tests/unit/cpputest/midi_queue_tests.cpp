#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue.h"
#include "libdeluge/system.h"
#include <cstdint>

namespace {

QueuedMIDI noteOn(uint8_t note, OutputTime due) {
	return QueuedMIDI{.message = MIDIMessage::noteOn(0, note, 100), .filter = 0, .usb = true, .din = true, .due = due};
}

} // namespace

TEST_GROUP(MidiQueueTests){};

TEST(MidiQueueTests, StartsEmpty) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	OutputTime due = 0;
	CHECK(queue.empty());
	CHECK_FALSE(queue.nextDue(due, guard));
	CHECK(queue.frontDue(0, true, guard) == nullptr);
}

TEST(MidiQueueTests, KeepsPayloadAndDueTime) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	CHECK(queue.push(noteOn(60, 1500), 1000, guard));
	OutputTime due = 0;
	CHECK(queue.nextDue(due, guard));
	CHECK_EQUAL(1500u, due);
	const QueuedMIDI* front = queue.frontDue(1500, false, guard);
	CHECK(front != nullptr);
	CHECK_EQUAL(60, front->message.data1);
	CHECK(front->usb);
	CHECK(front->din);
}

TEST(MidiQueueTests, OnlyDueMessagesComeOut) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	queue.push(noteOn(60, 1100), 1000, guard);
	queue.push(noteOn(61, 1200), 1000, guard);
	CHECK(queue.frontDue(1099, false, guard) == nullptr);
	CHECK(queue.frontDue(1100, false, guard) != nullptr);
	queue.pop(guard);
	CHECK(queue.frontDue(1150, false, guard) == nullptr);
	CHECK(queue.frontDue(1150, true, guard) != nullptr);
}

TEST(MidiQueueTests, EarlierDueBehindLaterWaitsToKeepOrder) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	queue.push(noteOn(60, 1500), 1000, guard);
	queue.push(noteOn(61, 1100), 1000, guard);
	// The second is due, but stays behind the first so nothing goes out of order
	CHECK(queue.frontDue(1200, false, guard) == nullptr);
	const QueuedMIDI* front = queue.frontDue(1500, false, guard);
	CHECK(front != nullptr);
	CHECK_EQUAL(60, front->message.data1);
}

TEST(MidiQueueTests, StaleDueTimesAreDueNow) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	// In the past
	queue.push(noteOn(60, 900), 1000, guard);
	// Further ahead than any audio buffer, e.g. left over from before the render stalled
	queue.push(noteOn(61, 1000 + MIDIQueue::kMaxMicrosAhead + 1), 1000, guard);
	OutputTime due = 0;
	queue.nextDue(due, guard);
	CHECK_EQUAL(1000u, due);
	queue.pop(guard);
	queue.nextDue(due, guard);
	CHECK_EQUAL(1000u, due);
}

TEST(MidiQueueTests, DueComparisonHandlesClockWrap) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	OutputTime now = UINT32_MAX - 100;
	queue.push(noteOn(60, now + 200), now, guard); // wraps past zero
	OutputTime due = 0;
	queue.nextDue(due, guard);
	CHECK_EQUAL(99u, due);
	CHECK(queue.frontDue(now + 199, false, guard) == nullptr);
	CHECK(queue.frontDue(now + 200, false, guard) != nullptr);
}

TEST(MidiQueueTests, FullQueueRejectsUntilSpaceFreed) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	for (size_t i = 0; i < MIDIQueue::kCapacity; i++) {
		CHECK(queue.push(noteOn(60, 1000), 1000, guard));
	}
	CHECK(queue.full());
	CHECK_FALSE(queue.push(noteOn(61, 1000), 1000, guard));
	queue.pop(guard);
	CHECK(queue.push(noteOn(61, 1000), 1000, guard));
}

TEST(MidiQueueTests, PopOldestMakesRoomRegardlessOfDueTime) {
	MIDIQueue queue;
	CriticalSectionGuard guard;
	for (size_t i = 0; i < MIDIQueue::kCapacity; i++) {
		CHECK(queue.push(noteOn(i % 128, 2000), 1000, guard));
	}
	CHECK_FALSE(queue.push(noteOn(127, 2000), 1000, guard));

	// None are due, but the oldest still comes out
	CHECK(queue.frontDue(1000, false, guard) == nullptr);
	QueuedMIDI oldest;
	CHECK(queue.popOldest(oldest, guard));
	CHECK_EQUAL(0, oldest.message.data1);
	CHECK(queue.push(noteOn(127, 2000), 1000, guard));

	// The new one went in behind everything already queued
	for (size_t i = 1; i < MIDIQueue::kCapacity; i++) {
		CHECK(queue.popOldest(oldest, guard));
		CHECK_EQUAL(i % 128, oldest.message.data1);
	}
	CHECK(queue.popOldest(oldest, guard));
	CHECK_EQUAL(127, oldest.message.data1);
	CHECK_FALSE(queue.popOldest(oldest, guard));
}

TEST_GROUP(OutputTimeTests){};

TEST(OutputTimeTests, WindowStartIsWhenThePlayheadReachesIt) {
	// 441 samples is 10ms
	CHECK_EQUAL(1000u + 10'000u, outputTimeAtWindowOffset(1000, 441, 0));
}

TEST(OutputTimeTests, OffsetsWithinTheWindowAreLaterBySampleCount) {
	OutputTime start = outputTimeAtWindowOffset(5000, 128, 0);
	OutputTime later = outputTimeAtWindowOffset(5000, 128, 441);
	// 441 samples later is 10ms, give or take the Q8 rounding of a sample's length
	CHECK(static_cast<int32_t>(later - start) >= 9'990);
	CHECK(static_cast<int32_t>(later - start) <= 10'010);
}

TEST(OutputTimeTests, NeverGoesBackwardsAcrossAWindow) {
	OutputTime previous = outputTimeAtWindowOffset(0, 64, 0);
	for (int32_t offset = 1; offset <= 128; offset++) {
		OutputTime next = outputTimeAtWindowOffset(0, 64, offset);
		CHECK(static_cast<int32_t>(next - previous) >= 0);
		previous = next;
	}
}
