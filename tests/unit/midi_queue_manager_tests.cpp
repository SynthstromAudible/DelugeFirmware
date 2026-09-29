#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue_manager.h"
#include "io/midi/midi_queue_transports.h"
#include <array>
#include <vector>

// These tests cover the transport-neutral pieces of the MIDI queue manager: the ring lane.
// Header-only, so they can be driven directly without the USB/UART layers.

namespace {

constexpr uint8_t kCCStatus = 0xB0; // CC on channel 0

/// Packs the identity fields the CC policy cares about into one queue entry.
constexpr uint32_t pack(uint8_t status, uint8_t cc_number, uint8_t value) {
	return (static_cast<uint32_t>(status) << 16) | (static_cast<uint32_t>(cc_number) << 8) | value;
}
constexpr uint8_t status_of(uint32_t e) {
	return static_cast<uint8_t>(e >> 16);
}
constexpr uint8_t cc_of(uint32_t e) {
	return static_cast<uint8_t>(e >> 8);
}
constexpr uint8_t value_of(uint32_t e) {
	return static_cast<uint8_t>(e);
}

/// A lane plus the storage it views, so tests can build one without a MIDIQueueStorage.
///
/// MIDIQueueLane is a view: the owning storage hands it a pointer and a wrap mask. This mirrors that
/// for the lane's own unit tests.
template <typename T, uint16_t Capacity>
struct OwnedLane {
	static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
	std::array<T, Capacity> storage{};
	MIDIQueueLane<T> lane{};

	OwnedLane() {
		lane.data = storage.data();
		lane.mask = Capacity - 1;
	}
	MIDIQueueLane<T>* operator->() { return &lane; }
	MIDIQueueLane<T>& operator*() { return lane; }
};

} // namespace

// --- Ring lane mechanics ---

TEST_GROUP(MIDIQueueLaneBasics){};

TEST(MIDIQueueLaneBasics, PushPopRoundTrip) {
	OwnedLane<uint32_t, 8> lane;
	CHECK_TRUE(lane->empty());
	CHECK_TRUE(lane->push(11));
	CHECK_TRUE(lane->push(22));
	CHECK_EQUAL(2, lane->size());

	uint32_t out = 0;
	CHECK_TRUE(lane->pop(out));
	CHECK_EQUAL(11, out);
	CHECK_TRUE(lane->pop(out));
	CHECK_EQUAL(22, out);
	CHECK_TRUE(lane->empty());
}

TEST(MIDIQueueLaneBasics, KeepsOneSlotFreeSoFullIsDistinctFromEmpty) {
	OwnedLane<uint32_t, 8> lane;
	for (uint32_t i = 0; i < 7; i++) {
		CHECK_TRUE(lane->push(i));
	}
	CHECK_EQUAL(7, lane->size());
	CHECK_EQUAL(0, lane->space());
	CHECK_FALSE(lane->push(99)); // capacity is Capacity-1
	CHECK_FALSE(lane->empty());
}

TEST(MIDIQueueLaneBasics, PeekIsRelativeToHeadAcrossWrap) {
	OwnedLane<uint32_t, 8> lane;
	// Drive read_pos forward so the logical span wraps the physical ring.
	for (uint32_t i = 0; i < 6; i++) {
		lane->push(i);
	}
	uint32_t sink = 0;
	for (int i = 0; i < 5; i++) {
		lane->pop(sink);
	}
	lane->push(100);
	lane->push(200);
	CHECK_EQUAL(3, lane->size());
	CHECK_EQUAL(5, lane->peek(0));
	CHECK_EQUAL(100, lane->peek(1));
	CHECK_EQUAL(200, lane->peek(2));
}

TEST(MIDIQueueLaneBasics, PopManyIsAllOrNothing) {
	OwnedLane<uint32_t, 8> lane;
	lane->push(1);
	lane->push(2);
	uint32_t out[3] = {0, 0, 0};
	CHECK_FALSE(lane->pop_many(out, 3)); // more than queued: must not partially consume
	CHECK_EQUAL(2, lane->size());
	CHECK_TRUE(lane->pop_many(out, 2));
	CHECK_EQUAL(1, out[0]);
	CHECK_EQUAL(2, out[1]);
	CHECK_TRUE(lane->empty());
}

// --- Message intent and lane classification ---
//
// Intent decides which lane a message lands in, and lanes are FIFO, so "must stay ordered" is
// expressed as "must share a lane".

TEST_GROUP(MIDIMessageClassification){};

TEST(MIDIMessageClassification, DefaultIntentIsEvent) {
	// A sender that says nothing must get the conservative behaviour.
	MIDIMessage m = MIDIMessage::cc(0, 74, 100);
	CHECK(m.intent == MIDIIntent::Event);
}

TEST(MIDIMessageClassification, ContinuousCCGoesToTheScheduledLane) {
	MIDIMessage m = MIDIMessage::cc(0, 20, 64);
	m.intent = MIDIIntent::Continuous;
	CHECK(MIDIQueueManager::classify_message(m) == QUEUE_PRIORITY_CC);
}

TEST(MIDIMessageClassification, EventCCAvoidsTheScheduledLane) {
	// Bank select, RPN and friends must be FIFO.
	MIDIMessage m = MIDIMessage::cc(0, 100, 6);
	CHECK(MIDIQueueManager::classify_message(m) == QUEUE_PRIORITY_EXPRESSION);
}

TEST(MIDIMessageClassification, NoteBoundSharesTheNotesLane) {
	// Expression that initialises a note, and All Notes Off, must not be overtaken by note traffic.
	MIDIMessage pitch = MIDIMessage::pitchBend(0, 8192);
	pitch.intent = MIDIIntent::NoteBound;
	CHECK(MIDIQueueManager::classify_message(pitch) == QUEUE_PRIORITY_NOTES);

	MIDIMessage allNotesOff = MIDIMessage::cc(0, 123, 0);
	allNotesOff.intent = MIDIIntent::NoteBound;
	CHECK(MIDIQueueManager::classify_message(allNotesOff) == QUEUE_PRIORITY_NOTES);
}

TEST(MIDIMessageClassification, ProgramChangeIsOrdered) {
	// Program change follows bank select, so it must not sit in the reorderable lane.
	MIDIMessage m = MIDIMessage::programChange(0, 5);
	CHECK(MIDIQueueManager::classify_message(m) == QUEUE_PRIORITY_EXPRESSION);
}

TEST(MIDIMessageClassification, UnchangedClassificationsStillHold) {
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::noteOn(0, 60, 100)) == QUEUE_PRIORITY_NOTES);
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::noteOff(0, 60, 0)) == QUEUE_PRIORITY_NOTES);
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::pitchBend(0, 8192)) == QUEUE_PRIORITY_EXPRESSION);
	CHECK(MIDIQueueManager::classify_message(MIDIMessage::channelAftertouch(0, 64)) == QUEUE_PRIORITY_EXPRESSION);

	// Mod wheel and MPE Y stay on the expression lane whatever their intent.
	MIDIMessage modWheel = MIDIMessage::cc(0, CC_EXTERNAL_MOD_WHEEL, 64);
	modWheel.intent = MIDIIntent::Continuous;
	CHECK(MIDIQueueManager::classify_message(modWheel) == QUEUE_PRIORITY_EXPRESSION);

	MIDIMessage mpeY = MIDIMessage::cc(0, CC_EXTERNAL_MPE_Y, 64);
	mpeY.intent = MIDIIntent::Continuous;
	CHECK(MIDIQueueManager::classify_message(mpeY) == QUEUE_PRIORITY_EXPRESSION);
}

// --- Regression tests for MIDI output ordering ---
//
// MIDI uses stateful prefixes: sequences where earlier messages establish context for later ones.
// each sequence below must be kept
//
// These are asserted at the classification level because lanes are FIFO: co-locating an ordered sequence
// on one lane is what keeps it ordered. See "Message intent" in docs/dev/systems/midi_queue_manager.md.

namespace {
/// Builds the CC an ordered protocol sequence sends: default Event intent.
MIDIMessage eventCC(uint8_t channel, uint8_t cc, uint8_t value) {
	return MIDIMessage::cc(channel, cc, value);
}
} // namespace

TEST_GROUP(MIDIOrderingRegressions){};

TEST(MIDIOrderingRegressions, RPNSequenceStaysOnOneOrderedLane) {
	// sendRPN() emits CC100, CC101, CC6, then CC100=127 and CC101=127 as a terminator.
	MIDIMessage sequence[] = {
	    eventCC(0, 100, 6), eventCC(0, 101, 0), eventCC(0, 6, 4), eventCC(0, 100, 127), eventCC(0, 101, 127),
	};
	for (MIDIMessage m : sequence) {
		QueuePriority lane = MIDIQueueManager::classify_message(m);
		CHECK(lane != QUEUE_PRIORITY_CC); // must be in FIFO lane
		CHECK(lane == QUEUE_PRIORITY_EXPRESSION);
	}
}

TEST(MIDIOrderingRegressions, MPENoteInitialisationSharesTheNoteLane) {
	// outputAllMPEValuesOnMemberChannel() sends these immediately before a note-on. The notes lane
	// outranks the expression lane, so without NoteBound the note-on overtook them.
	MIDIMessage x = MIDIMessage::pitchBend(1, 8192);
	MIDIMessage y = MIDIMessage::cc(1, CC_EXTERNAL_MPE_Y, 64);
	MIDIMessage z = MIDIMessage::channelAftertouch(1, 0);
	x.intent = MIDIIntent::NoteBound;
	y.intent = MIDIIntent::NoteBound;
	z.intent = MIDIIntent::NoteBound;

	QueuePriority noteLane = MIDIQueueManager::classify_message(MIDIMessage::noteOn(1, 60, 100));
	CHECK(MIDIQueueManager::classify_message(x) == noteLane);
	CHECK(MIDIQueueManager::classify_message(y) == noteLane);
	CHECK(MIDIQueueManager::classify_message(z) == noteLane);
}

TEST(MIDIOrderingRegressions, BankSelectAndProgramChangeShareAnOrderedLane) {
	// instrument_clip.cpp sends bank MSB, bank LSB, then the program change. These must stay ordered: a
	// reordered CC could otherwise be pulled ahead of the program change and land on the old patch.
	QueuePriority bankMSB = MIDIQueueManager::classify_message(eventCC(0, 0, 3));
	QueuePriority bankLSB = MIDIQueueManager::classify_message(eventCC(0, 32, 1));
	QueuePriority pgm = MIDIQueueManager::classify_message(MIDIMessage::programChange(0, 5));

	CHECK(bankMSB == bankLSB);
	CHECK(bankLSB == pgm);
	CHECK(pgm != QUEUE_PRIORITY_CC);
}

TEST(MIDIOrderingRegressions, AllNotesOffSharesTheNoteLane) {
	// All Notes Off must share the notes lane: on a lower-priority lane it would drain after notes sent
	// afterward, silencing them instead of the notes it was meant to stop.
	MIDIMessage allNotesOff = MIDIMessage::cc(0, 123, 0);
	allNotesOff.intent = MIDIIntent::NoteBound;
	CHECK(MIDIQueueManager::classify_message(allNotesOff)
	      == MIDIQueueManager::classify_message(MIDIMessage::noteOn(0, 60, 100)));
}

TEST(MIDIOrderingRegressions, MomentaryCCKeepsBothOfItsValues) {
	// A CC used as a trigger sends 127 then 0. Event intent keeps it off the cc lane entirely.
	CHECK(MIDIQueueManager::classify_message(eventCC(0, 64, 127)) != QUEUE_PRIORITY_CC);
	CHECK(MIDIQueueManager::classify_message(eventCC(0, 64, 0)) != QUEUE_PRIORITY_CC);
}

// --- Enqueue reports backpressure instead of flushing ---
//
// The queue manager must not call back into MidiEngine: doing so would turn the call graph into a
// cycle and let a mainline enqueue synchronously trigger the interrupt-masked drain. Enqueue reports
// that a flush is wanted and the caller decides.

TEST_GROUP(MIDIQueueBackpressure){};

TEST(MIDIQueueBackpressure, EnqueueRequestsFlushOnlyOnceBacklogIsHigh) {
	MIDIQueueManagerUSB queue;
	queue.reset_queue_storage();

	// A single message is not backlog.
	CHECK_FALSE(queue.enqueue_message(0x09903C64, MIDIIntent::Event));

	// Past k_usb_flush_backlog_message_threshold (16) it should ask for a flush.
	bool asked = false;
	for (int i = 0; i < 32; i++) {
		asked = queue.enqueue_message(0x09903C64, MIDIIntent::Event) || asked;
	}
	CHECK_TRUE(asked);
}

// --- Per-lane capacity ---

TEST_GROUP(MIDIQueueLaneCapacity){};

TEST(MIDIQueueLaneCapacity, EveryCapacityIsAPowerOfTwo) {
	// The ring masks positions instead of taking a modulo, so this is a correctness requirement.
	for (int i = 0; i < QUEUE_PRIORITY_COUNT; i++) {
		uint16_t usb = k_usb_lane_capacity[i];
		uint16_t din = k_din_lane_capacity[i];
		CHECK(usb != 0 && (usb & (usb - 1)) == 0);
		CHECK(din != 0 && (din & (din - 1)) == 0);
	}
}

TEST(MIDIQueueLaneCapacity, SysExLaneHoldsACompleteMaximumStream) {
	// The largest stream the firmware stages is MidiEngine::sysex_fmt_buffer[1024].
	constexpr int kMaxSysExStreamBytes = 1024;

	// DIN queues raw bytes, so one byte per slot. enqueue_sysex is all-or-nothing, so a lane that
	// cannot hold the largest stream would silently drop it. One slot is always reserved, hence the
	// strict comparison.
	CHECK(k_din_lane_capacity[QUEUE_PRIORITY_SYSEX] > kMaxSysExStreamBytes);

	// USB queues packed USB-MIDI events carrying up to three payload bytes each, so the same stream
	// needs ceil(1024/3) = 342 events. MICableUSB::sendSysex() discards every enqueue result, so a lane
	// too small to take a whole frame drops events mid-stream and truncates the SysEx on the wire.
	constexpr int kMaxSysExStreamEvents = (kMaxSysExStreamBytes + 2) / 3;
	CHECK(k_usb_lane_capacity[QUEUE_PRIORITY_SYSEX] > kMaxSysExStreamEvents);
}

// --- Transport traits ---
//
// Both transports are driven through the same CC-lane policy; these pin the four operations that
// actually differ, so a mistake in either traits struct fails here rather than as scrambled MIDI.

TEST_GROUP(MIDITransportTraits){};

TEST(MIDITransportTraits, UsbTraitsReadAndRewriteAPackedEvent) {
	// byte0 cable/CIN, byte1 status, byte2 CC number, byte3 value
	uint32_t e = (uint32_t{64} << 24) | (uint32_t{74} << 16) | (uint32_t{0xB0} << 8) | 0x0B;
	CHECK_TRUE(UsbTransport::is_channel_cc(&e));
	CHECK_EQUAL(0xB0, UsbTransport::status(&e));
	CHECK_EQUAL(74, UsbTransport::cc_number(&e));

	UsbTransport::set_value(&e, 127);
	CHECK_EQUAL(127, static_cast<uint8_t>(e >> 24));
	CHECK_EQUAL(0xB0, UsbTransport::status(&e)); // identity untouched
	CHECK_EQUAL(74, UsbTransport::cc_number(&e));
	CHECK_EQUAL(0x0B, static_cast<uint8_t>(e & 0xFF)); // cable/CIN untouched
}

TEST(MIDITransportTraits, DinTraitsReadAndRewriteAThreeByteMessage) {
	uint8_t m[3] = {0xB0, 74, 64};
	CHECK_TRUE(DinTransport::is_channel_cc(m));
	CHECK_EQUAL(0xB0, DinTransport::status(m));
	CHECK_EQUAL(74, DinTransport::cc_number(m));

	DinTransport::set_value(m, 127);
	CHECK_EQUAL(127, m[2]);
	CHECK_EQUAL(0xB0, m[0]);
	CHECK_EQUAL(74, m[1]);
}

TEST(MIDITransportTraits, SpansMatchTheStorageUnit) {
	CHECK_EQUAL(1, UsbTransport::cc_span);
	CHECK_EQUAL(3, DinTransport::cc_span);
}
