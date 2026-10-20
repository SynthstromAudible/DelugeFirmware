#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue.h"
#include "model/midi/message.h"
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

constexpr int32_t kMaxTicksAhead = MIDIQueue::kMaxTicksAhead;
constexpr int32_t kTxBufferNumSamples = SSI_TX_BUFFER_NUM_SAMPLES;

/// Drives the firmware MIDIQueue the way MidiEngine does, with the system timer passed in and the outputs captured
/// rather than sent to USB / DIN
class TestMidiQueue {
public:
	MIDIQueue queue;
	uint32_t midiDue_{0};
	std::vector<QueuedMIDI> sent;

	void setMIDIDue(uint32_t due) { midiDue_ = due; }

	bool queueMIDI(MIDIMessage message, int32_t filter, bool sendUSB, bool sendDIN, uint32_t now) {
		return queue.push(message, filter, sendUSB, sendDIN, midiDue_, now);
	}

	bool nextMIDIDue(uint32_t& due) { return queue.nextDue(due); }

	/// Returns how many were sent
	size_t flushQueuedMIDI(bool all, uint32_t now) {
		size_t count = 0;
		QueuedMIDI queued;
		while (queue.popDue(queued, now, all)) {
			sent.push_back(queued);
			count++;
		}
		return count;
	}

	size_t flushDueMIDI(uint32_t now) { return flushQueuedMIDI(false, now); }
	size_t flushAllQueuedMIDI() { return flushQueuedMIDI(true, 0); }
};

} // namespace

// ============================================================================
// MIDIMessage & USB Formatting Tests
// ============================================================================

TEST_GROUP(MIDIMessageTests){};

TEST(MIDIMessageTests, NoteOnFormatting) {
	MIDIMessage msg = MIDIMessage::noteOn(2, 60, 100);
	CHECK_EQUAL(0x09, msg.statusType);
	CHECK_EQUAL(2, msg.channel);
	CHECK_EQUAL(60, msg.data1);
	CHECK_EQUAL(100, msg.data2);
	CHECK_FALSE(msg.isSystemMessage());
	CHECK_EQUAL(3, bytesPerStatusMessage(0x92));
}

TEST(MIDIMessageTests, NoteOffFormatting) {
	MIDIMessage msg = MIDIMessage::noteOff(0, 72, 64);
	CHECK_EQUAL(0x08, msg.statusType);
	CHECK_EQUAL(0, msg.channel);
	CHECK_EQUAL(72, msg.data1);
	CHECK_EQUAL(64, msg.data2);
	CHECK_FALSE(msg.isSystemMessage());
	CHECK_EQUAL(3, bytesPerStatusMessage(0x80));
}

TEST(MIDIMessageTests, ControlChangeFormatting) {
	MIDIMessage msg = MIDIMessage::cc(5, 74, 120);
	CHECK_EQUAL(0x0B, msg.statusType);
	CHECK_EQUAL(5, msg.channel);
	CHECK_EQUAL(74, msg.data1);
	CHECK_EQUAL(120, msg.data2);
	CHECK_EQUAL(3, bytesPerStatusMessage(0xB5));
}

TEST(MIDIMessageTests, ProgramChangeFormatting) {
	MIDIMessage msg = MIDIMessage::programChange(3, 42);
	CHECK_EQUAL(0x0C, msg.statusType);
	CHECK_EQUAL(3, msg.channel);
	CHECK_EQUAL(42, msg.data1);
	CHECK_EQUAL(0, msg.data2);
	CHECK_EQUAL(2, bytesPerStatusMessage(0xC3));
}

TEST(MIDIMessageTests, PitchBend14BitFormatting) {
	// Center bend = 8192 (0x2000): LSB = 0x00, MSB = 0x40
	MIDIMessage msgCenter = MIDIMessage::pitchBend(1, 8192);
	CHECK_EQUAL(0x0E, msgCenter.statusType);
	CHECK_EQUAL(1, msgCenter.channel);
	CHECK_EQUAL(0x00, msgCenter.data1);
	CHECK_EQUAL(0x40, msgCenter.data2);

	// Max bend = 16383 (0x3FFF): LSB = 0x7F, MSB = 0x7F
	MIDIMessage msgMax = MIDIMessage::pitchBend(1, 16383);
	CHECK_EQUAL(0x7F, msgMax.data1);
	CHECK_EQUAL(0x7F, msgMax.data2);
	CHECK_EQUAL(3, bytesPerStatusMessage(0xE1));
}

TEST(MIDIMessageTests, RealtimeSystemMessages) {
	MIDIMessage clock = MIDIMessage::realtimeClock();
	CHECK_EQUAL(0x0F, clock.statusType);
	CHECK_EQUAL(0x08, clock.channel);
	CHECK_TRUE(clock.isSystemMessage());
	CHECK_EQUAL(1, bytesPerStatusMessage(0xF8));

	MIDIMessage start = MIDIMessage::realtimeStart();
	CHECK_EQUAL(0x0A, start.channel);
	CHECK_EQUAL(1, bytesPerStatusMessage(0xFA));

	MIDIMessage cont = MIDIMessage::realtimeContinue();
	CHECK_EQUAL(0x0B, cont.channel);
	CHECK_EQUAL(1, bytesPerStatusMessage(0xFB));

	MIDIMessage stop = MIDIMessage::realtimeStop();
	CHECK_EQUAL(0x0C, stop.channel);
	CHECK_EQUAL(1, bytesPerStatusMessage(0xFC));
}

TEST(MIDIMessageTests, SongPositionPointerFormatting) {
	// Position 0x0123 -> LSB = 0x23, MSB = 0x02
	MIDIMessage spp = MIDIMessage::systemPositionPointer(0x0123);
	CHECK_EQUAL(0x0F, spp.statusType);
	CHECK_EQUAL(0x02, spp.channel);
	CHECK_EQUAL(0x23, spp.data1);
	CHECK_EQUAL(0x02, spp.data2);
	CHECK_EQUAL(3, bytesPerStatusMessage(0xF2));

	uint32_t usbPacket = setupUSBMessage(spp);
	// CIN for Position Pointer (0xF2) should be 0x03
	CHECK_EQUAL(0x03, usbPacket & 0xFF);
	CHECK_EQUAL(0xF2, (usbPacket >> 8) & 0xFF);
	CHECK_EQUAL(0x23, (usbPacket >> 16) & 0xFF);
	CHECK_EQUAL(0x02, (usbPacket >> 24) & 0xFF);
}

TEST(MIDIMessageTests, USBMessageVoicePackaging) {
	MIDIMessage noteOn = MIDIMessage::noteOn(4, 64, 127);
	uint32_t usbPacket = setupUSBMessage(noteOn);

	// CIN = statusType = 0x09
	// byte0: CIN (0x09)
	// byte1: status (0x94)
	// byte2: note (64 = 0x40)
	// byte3: velocity (127 = 0x7F)
	CHECK_EQUAL(0x09, usbPacket & 0xFF);
	CHECK_EQUAL(0x94, (usbPacket >> 8) & 0xFF);
	CHECK_EQUAL(0x40, (usbPacket >> 16) & 0xFF);
	CHECK_EQUAL(0x7F, (usbPacket >> 24) & 0xFF);
}

TEST(MIDIMessageTests, DataByteSaturationClamping) {
	CHECK_EQUAL(0, toDataByte(-100));
	CHECK_EQUAL(0, toDataByte(0));
	CHECK_EQUAL(64, toDataByte(64));
	CHECK_EQUAL(127, toDataByte(127));
	CHECK_EQUAL(127, toDataByte(255));
	CHECK_EQUAL(127, toDataByte(10000));
}

// ============================================================================
// MIDI Queue Basic Operations
// ============================================================================

TEST_GROUP(MidiQueueBasicTests){};

TEST(MidiQueueBasicTests, InitialQueueState) {
	TestMidiQueue mq;
	CHECK_TRUE(mq.queue.empty());
	CHECK_EQUAL(256, MIDIQueue::kCapacity);
	uint32_t due = 0;
	CHECK_FALSE(mq.nextMIDIDue(due));
	CHECK_EQUAL(0, mq.flushDueMIDI(0));
}

TEST(MidiQueueBasicTests, MessageKeepsPayloadAndDueTime) {
	TestMidiQueue mq;
	uint32_t now = 1000;
	mq.setMIDIDue(now + 500);
	CHECK_TRUE(mq.queueMIDI(MIDIMessage::noteOn(1, 60, 100), -1, true, false, now));

	uint32_t due = 0;
	CHECK_TRUE(mq.nextMIDIDue(due));
	CHECK_EQUAL(now + 500, due);

	CHECK_EQUAL(1, mq.flushDueMIDI(now + 500));
	const QueuedMIDI& out = mq.sent[0];
	CHECK_EQUAL(0x09, out.message.statusType);
	CHECK_EQUAL(1, out.message.channel);
	CHECK_EQUAL(60, out.message.data1);
	CHECK_EQUAL(100, out.message.data2);
	CHECK_EQUAL(-1, out.filter);
	CHECK_TRUE(out.usb);
	CHECK_FALSE(out.din);
	CHECK_EQUAL(now + 500, out.due);
	CHECK_TRUE(mq.queue.empty());
}

TEST(MidiQueueBasicTests, FullQueueRejectsUntilSpaceFreed) {
	TestMidiQueue mq;
	for (uint32_t i = 0; i < 256; ++i) {
		CHECK_TRUE(mq.queueMIDI(MIDIMessage::cc(0, i % 128, 0), 0, true, true, 0));
	}
	CHECK_TRUE(mq.queue.full());
	CHECK_FALSE(mq.queueMIDI(MIDIMessage::noteOn(0, 48, 100), 0, true, true, 0));

	CHECK_EQUAL(256, mq.flushDueMIDI(0));
	CHECK_TRUE(mq.queueMIDI(MIDIMessage::noteOn(0, 48, 100), 0, true, true, 0));
}

TEST(MidiQueueBasicTests, PopOldestMakesRoomRegardlessOfDueTime) {
	TestMidiQueue mq;
	uint32_t now = 0;
	mq.setMIDIDue(now + 1000);
	for (uint32_t i = 0; i < 256; ++i) {
		CHECK_TRUE(mq.queueMIDI(MIDIMessage::cc(0, i % 128, 0), 0, true, true, now));
	}
	CHECK_FALSE(mq.queueMIDI(MIDIMessage::noteOn(0, 48, 100), 0, true, true, now));

	// None are due, but the oldest still comes out
	CHECK_EQUAL(0, mq.flushDueMIDI(now));
	QueuedMIDI oldest;
	CHECK_TRUE(mq.queue.popOldest(oldest));
	CHECK_EQUAL(0, oldest.message.data1);
	CHECK_TRUE(mq.queueMIDI(MIDIMessage::noteOn(0, 48, 100), 0, true, true, now));

	CHECK_EQUAL(256, mq.flushAllQueuedMIDI());
	CHECK_EQUAL(1, mq.sent[0].message.data1);
	CHECK_EQUAL(0x09, mq.sent[255].message.statusType);

	CHECK_FALSE(mq.queue.popOldest(oldest));
}

TEST(MidiQueueBasicTests, WrapAroundFifoIntegrity) {
	TestMidiQueue mq;
	constexpr uint32_t kTotalMessages = 2000;
	uint32_t pushed = 0;
	while (pushed < kTotalMessages) {
		for (uint32_t i = 0; i < 10; ++i, ++pushed) {
			CHECK_TRUE(mq.queueMIDI(MIDIMessage::noteOn(pushed % 16, pushed % 128, (pushed * 3) % 128),
			                        static_cast<int16_t>(pushed), true, false, 0));
		}
		mq.flushDueMIDI(0);
	}
	CHECK_EQUAL(kTotalMessages, mq.sent.size());
	for (uint32_t i = 0; i < kTotalMessages; ++i) {
		CHECK_EQUAL(i % 16, mq.sent[i].message.channel);
		CHECK_EQUAL(i % 128, mq.sent[i].message.data1);
		CHECK_EQUAL(static_cast<int16_t>(i), mq.sent[i].filter);
	}
}

// ============================================================================
// Due Time Handling
// ============================================================================

TEST_GROUP(MidiQueueDueTests){};

TEST(MidiQueueDueTests, OnlyDueMessagesAreSent) {
	TestMidiQueue mq;
	uint32_t now = 10000;
	mq.setMIDIDue(now + 100);
	mq.queueMIDI(MIDIMessage::noteOn(0, 60, 100), 0, true, false, now);
	mq.setMIDIDue(now + 200);
	mq.queueMIDI(MIDIMessage::noteOn(0, 64, 100), 0, true, false, now);
	mq.setMIDIDue(now + 300);
	mq.queueMIDI(MIDIMessage::noteOn(0, 67, 100), 0, true, false, now);

	// Nothing due yet
	CHECK_EQUAL(0, mq.flushDueMIDI(now + 99));

	// Exactly due counts
	CHECK_EQUAL(1, mq.flushDueMIDI(now + 100));
	CHECK_EQUAL(60, mq.sent[0].message.data1);

	uint32_t due = 0;
	CHECK_TRUE(mq.nextMIDIDue(due));
	CHECK_EQUAL(now + 200, due);

	// Late flush takes everything that's become due
	CHECK_EQUAL(2, mq.flushDueMIDI(now + 1000));
	CHECK_EQUAL(64, mq.sent[1].message.data1);
	CHECK_EQUAL(67, mq.sent[2].message.data1);
	CHECK_FALSE(mq.nextMIDIDue(due));
}

// n.b. - this may not be desireable in the long term. Right now the queue is absolute with no reordering.
TEST(MidiQueueDueTests, EarlierDueBehindLaterWaitsToKeepOrder) {
	TestMidiQueue mq;
	uint32_t now = 0;
	mq.setMIDIDue(now + 500);
	mq.queueMIDI(MIDIMessage::noteOn(0, 60, 100), 0, true, false, now);
	mq.setMIDIDue(now + 100);
	mq.queueMIDI(MIDIMessage::noteOff(0, 60, 64), 0, true, false, now);

	// The note off is due, but the note on ahead of it isn't, so neither goes - order is preserved
	CHECK_EQUAL(0, mq.flushDueMIDI(now + 200));
	CHECK_EQUAL(2, mq.flushDueMIDI(now + 500));
	CHECK_EQUAL(0x09, mq.sent[0].message.statusType);
	CHECK_EQUAL(0x08, mq.sent[1].message.statusType);
}

TEST(MidiQueueDueTests, StaleDueTimesAreSentImmediately) {
	TestMidiQueue mq;
	uint32_t now = 5000000;

	// In the past
	mq.setMIDIDue(now - 1000);
	mq.queueMIDI(MIDIMessage::noteOn(0, 60, 100), 0, true, false, now);
	// Far in the future, e.g. what a past time looks like once the timer's wrapped
	mq.setMIDIDue(now + 0x80000000u + 1000);
	mq.queueMIDI(MIDIMessage::noteOn(0, 61, 100), 0, true, false, now);
	// Just beyond the allowed lookahead
	mq.setMIDIDue(now + kMaxTicksAhead + 1);
	mq.queueMIDI(MIDIMessage::noteOn(0, 62, 100), 0, true, false, now);
	// At the allowed lookahead - kept
	mq.setMIDIDue(now + kMaxTicksAhead);
	mq.queueMIDI(MIDIMessage::noteOn(0, 63, 100), 0, true, false, now);

	CHECK_EQUAL(3, mq.flushDueMIDI(now));
	CHECK_EQUAL(now, mq.sent[0].due);
	CHECK_EQUAL(now, mq.sent[1].due);
	CHECK_EQUAL(now, mq.sent[2].due);
	CHECK_EQUAL(1, mq.flushDueMIDI(now + kMaxTicksAhead));
	CHECK_EQUAL(63, mq.sent[3].message.data1);
}

TEST(MidiQueueDueTests, DueComparisonHandlesTimerWrap) {
	TestMidiQueue mq;
	uint32_t now = 0xFFFFFF00u;
	mq.setMIDIDue(now + 0x200); // wraps past zero
	mq.queueMIDI(MIDIMessage::noteOn(0, 60, 100), 0, true, false, now);
	uint32_t due = 0;
	CHECK_TRUE(mq.nextMIDIDue(due));
	CHECK_EQUAL(0x100u, due);

	CHECK_EQUAL(0, mq.flushDueMIDI(now + 0x1FF));
	CHECK_EQUAL(1, mq.flushDueMIDI(now + 0x200));
}

TEST(MidiQueueDueTests, FlushAllIgnoresDueTimes) {
	TestMidiQueue mq;
	uint32_t now = 0;
	for (uint32_t i = 0; i < 15; ++i) {
		mq.setMIDIDue(now + 1000 * (i + 1));
		mq.queueMIDI(MIDIMessage::cc(0, i, 127), 0, true, false, now);
	}
	CHECK_EQUAL(0, mq.flushDueMIDI(now));
	CHECK_EQUAL(15, mq.flushAllQueuedMIDI());
	CHECK_TRUE(mq.queue.empty());
	for (uint32_t i = 0; i < 15; ++i) {
		CHECK_EQUAL(i, mq.sent[i].message.data1);
	}
}

// ============================================================================
// Render Window Offset -> Due Time
// ============================================================================

TEST_GROUP(MidiWindowDueTests){};

TEST(MidiWindowDueTests, WindowStartIsWhenPlayheadReachesBufferedAudioEnd) {
	// 100 samples free in the buffer means 28 samples are still to play before the window's first sample
	uint32_t start = 1000000;
	uint32_t due = dueAtWindowOffset(start, 100, 0);
	CHECK_EQUAL(start + ((28 * kSystemTicksPerSampleQ8) >> 8), due);
}

TEST(MidiWindowDueTests, OffsetsWithinWindowAreLaterBySampleCount) {
	uint32_t start = 1000000;
	uint32_t at0 = dueAtWindowOffset(start, 100, 0);
	uint32_t at40 = dueAtWindowOffset(start, 100, 40);
	uint32_t delta = at40 - at0;
	// Within rounding of 40 samples
	uint32_t expected = (40 * kSystemTicksPerSampleQ8) >> 8;
	CHECK(delta + 1 >= expected && delta <= expected + 1);
}

TEST(MidiWindowDueTests, EndOfWindowFillingWholeBufferIsAFullLap) {
	// Window fills all the free space - its end is where the playhead is now, so a full buffer away
	uint32_t start = 0;
	uint32_t due = dueAtWindowOffset(start, 100, 100);
	CHECK_EQUAL((kTxBufferNumSamples * kSystemTicksPerSampleQ8) >> 8, due);
}

TEST(MidiWindowDueTests, EndOfWindowMatchesNextWindowStart) {
	// A 60 sample window out of 100 free; next time round 40 are still free plus whatever the playhead's consumed
	uint32_t start = 0;
	uint32_t endOfThis = dueAtWindowOffset(start, 100, 60);
	// 1ms later the playhead's moved on ~44 samples, so 84 are free
	uint32_t elapsedSamples = 44;
	uint32_t nextStart = start + ((elapsedSamples * kSystemTicksPerSampleQ8) >> 8);
	uint32_t startOfNext = dueAtWindowOffset(nextStart, 40 + elapsedSamples, 0);
	int32_t diff = (int32_t)(startOfNext - endOfThis);
	CHECK(diff >= -2 && diff <= 2);
}

TEST(MidiWindowDueTests, OffsetsPastFreeSpacePlayOnTheFollowingLap) {
	// A 40 sample window doubled to 74: offsets past 40 are only written once the playhead has passed them, so they
	// play after it laps again rather than straight away
	CHECK_EQUAL(kTxBufferNumSamples + 20, samplesTilWindowOffsetPlays(40, 60));
	CHECK_EQUAL(kTxBufferNumSamples + 34, samplesTilWindowOffsetPlays(40, 74));
}

TEST(MidiWindowDueTests, DueTimesNeverGoBackwardsAcrossAWindow) {
	// Every offset of a window doubled past its free space, including either side of where the free space ends. The
	// routine masks the free space to the buffer size, so it's never a whole buffer
	for (int32_t samplesBeforeLapping : {1, 7, 40, 64, kTxBufferNumSamples - 1}) {
		uint32_t previous = dueAtWindowOffset(0, samplesBeforeLapping, 0);
		for (int32_t offset = 1; offset <= kTxBufferNumSamples; offset++) {
			uint32_t due = dueAtWindowOffset(0, samplesBeforeLapping, offset);
			CHECK((int32_t)(due - previous) >= 0);
			previous = due;
		}
	}
}

TEST(MidiWindowDueTests, DueTimesStayWithinMaxTicksAhead) {
	// Otherwise the queue would treat them as stale and send them straight away
	for (int32_t samplesBeforeLapping : {1, kTxBufferNumSamples - 1}) {
		for (int32_t offset : {0, kTxBufferNumSamples}) {
			uint32_t due = dueAtWindowOffset(0, samplesBeforeLapping, offset);
			CHECK((int32_t)due > 0 && (int32_t)due <= kMaxTicksAhead);
		}
	}
}

// ============================================================================
// Multi-threaded Producer / Consumer
// ============================================================================

TEST_GROUP(MidiQueueConcurrencyTests){};

TEST(MidiQueueConcurrencyTests, ProducerQueuesWhileConsumerFlushesDue) {
	TestMidiQueue mq;
	constexpr uint32_t kTotalMessages = 30000;
	std::atomic<uint32_t> clock{0};
	std::atomic<bool> producerDone{false};
	bool sentEarly = false;
	mq.sent.reserve(kTotalMessages);

	// Audio engine: stamps each message slightly ahead of the current time
	std::thread producer([&]() {
		for (uint32_t sent = 0; sent < kTotalMessages; ++sent) {
			uint32_t now = clock.load(std::memory_order_acquire);
			mq.setMIDIDue(now + (sent % 50));
			MIDIMessage msg = MIDIMessage::noteOn(sent % 16, sent % 128, (sent * 7) % 128);
			while (!mq.queueMIDI(msg, static_cast<int16_t>(sent), sent % 2 == 0, sent % 3 == 0, now)) {
				std::this_thread::yield();
			}
		}
		producerDone.store(true, std::memory_order_release);
	});

	// Timer ISR: advances time and sends what's due
	std::thread consumer([&]() {
		while (!producerDone.load(std::memory_order_acquire) || !mq.queue.empty()) {
			uint32_t now = clock.fetch_add(10, std::memory_order_acq_rel) + 10;
			for (size_t i = mq.sent.size(), n = i + mq.flushDueMIDI(now); i < n; ++i) {
				// Never sent early. Only checked once joined - test macros aren't safe off the main thread
				sentEarly |= (int32_t)(mq.sent[i].due - now) > 0;
			}
			std::this_thread::yield();
		}
	});

	producer.join();
	consumer.join();

	CHECK_FALSE(sentEarly);
	CHECK_EQUAL(kTotalMessages, mq.sent.size());
	for (uint32_t i = 0; i < kTotalMessages; ++i) {
		const QueuedMIDI& item = mq.sent[i];
		CHECK_EQUAL(i % 16, item.message.channel);
		CHECK_EQUAL(i % 128, item.message.data1);
		CHECK_EQUAL((i * 7) % 128, item.message.data2);
		CHECK_EQUAL(static_cast<int16_t>(i), item.filter);
		CHECK_EQUAL(i % 2 == 0, item.usb);
		CHECK_EQUAL(i % 3 == 0, item.din);
	}
}
