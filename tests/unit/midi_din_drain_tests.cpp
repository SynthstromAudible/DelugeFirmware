#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue_manager.h"
#include "mocks/midi_transport_mock.h"

// Drives the real DIN drain path against a captured UART. This is where the ordering and concurrency
// defects found in review lived, and it is only reachable on the host because bufferMIDIUart is a
// substitutable function and the three remaining hardware symbols have link-time doubles.

TEST_GROUP(MIDIDinDrain) {
	MIDIQueueManagerDIN queue;

	void setup() {
		MidiTransportMock::reset();
		queue.reset_queue_storage();
		queue.reset_serial_state(0);
	}

	/// Drains repeatedly, advancing the sample timer so the DIN send allowance accrues.
	void drain(int passes) {
		for (int i = 1; i <= passes; i++) {
			queue.consume_queued_messages(static_cast<uint32_t>(i) * 48000u);
		}
	}
};

TEST(MIDIDinDrain, ClockOvertakesQueuedCC) {
	// A CC queued first must not delay a clock queued after it: that is the point of the lanes.
	MIDIMessage cc = MIDIMessage::cc(0, 20, 64);
	cc.intent = MIDIIntent::Continuous;
	queue.enqueue_message(cc);
	queue.enqueue_message(MIDIMessage{.statusType = 0x0F, .channel = 0x08, .data1 = 0, .data2 = 0});

	drain(4);

	auto const& sent = MidiTransportMock::sent_bytes();
	CHECK(sent.size() >= 1);
	CHECK_EQUAL(0xF8, sent[0]); // clock first, despite being queued second
}

TEST(MIDIDinDrain, EventCCsKeepTheirOrderAndTheirDuplicateValues) {
	// The RPN case end to end: five CCs, two of which repeat a CC number with a different value. If
	// coalescing reached these, the terminator would overwrite the selection and MPE configuration
	// would silently never apply.
	uint8_t const ccs[][2] = {{100, 6}, {101, 0}, {6, 4}, {100, 127}, {101, 127}};
	for (auto const& c : ccs) {
		queue.enqueue_message(MIDIMessage::cc(0, c[0], c[1])); // default Event intent
	}

	drain(16);

	auto const& sent = MidiTransportMock::sent_bytes();
	CHECK_EQUAL(15, sent.size()); // five complete 3-byte messages, none merged
	for (int i = 0; i < 5; i++) {
		CHECK_EQUAL(0xB0, sent[i * 3]);
		CHECK_EQUAL(ccs[i][0], sent[i * 3 + 1]);
		CHECK_EQUAL(ccs[i][1], sent[i * 3 + 2]);
	}
}

TEST(MIDIDinDrain, ContinuousCCsAreCoalescedToTheLatestValue) {
	// The counterpart: automation output declares itself Continuous, so repeated values for one CC
	// collapse instead of flooding the link.
	for (uint8_t v = 0; v < 32; v++) {
		MIDIMessage cc = MIDIMessage::cc(0, 20, v);
		cc.intent = MIDIIntent::Continuous;
		queue.enqueue_message(cc);
	}

	drain(16);

	auto const& sent = MidiTransportMock::sent_bytes();
	CHECK_EQUAL(3, sent.size()); // one message, not thirty-two
	CHECK_EQUAL(0xB0, sent[0]);
	CHECK_EQUAL(20, sent[1]);
	CHECK_EQUAL(31, sent[2]); // the newest value
}

TEST(MIDIDinDrain, SysExIsNotInterleaved) {
	uint8_t const sysex[] = {0xF0, 0x7D, 0x01, 0x02, 0xF7};
	CHECK_TRUE(queue.enqueue_sysex(sysex, sizeof(sysex)));
	MIDIMessage cc = MIDIMessage::cc(0, 20, 64);
	cc.intent = MIDIIntent::Continuous;
	queue.enqueue_message(cc);

	drain(16);

	// Once the stream starts, every byte through 0xF7 must be SysEx.
	auto const& sent = MidiTransportMock::sent_bytes();
	size_t start = 0;
	while (start < sent.size() && sent[start] != 0xF0) {
		start++;
	}
	CHECK(start + sizeof(sysex) <= sent.size());
	for (size_t i = 0; i < sizeof(sysex); i++) {
		CHECK_EQUAL(sysex[i], sent[start + i]);
	}
}

TEST(MIDIDinDrain, MalformedSysExIsRejectedRatherThanQueued) {
	// enqueue_sysex is all-or-nothing and requires its 0xF0/0xF7 framing; a partial stream would
	// strand the drain lock.
	uint8_t const no_start[] = {0x7D, 0x01, 0xF7};
	uint8_t const no_end[] = {0xF0, 0x7D, 0x01};
	CHECK_FALSE(queue.enqueue_sysex(no_start, sizeof(no_start)));
	CHECK_FALSE(queue.enqueue_sysex(no_end, sizeof(no_end)));
	CHECK_FALSE(queue.has_serial_data());
}

TEST(MIDIDinDrain, BlockedCCLaneDoesNotStarveSysEx) {
	// A CC lane that is merely blocked by its send allowance must not stop the pass: lower-priority
	// SysEx can still make progress. USB already falls through; DIN used to halt the whole pass.
	for (int i = 0; i < 40; i++) {
		MIDIMessage cc = MIDIMessage::cc(0, static_cast<uint8_t>(i), 64);
		cc.intent = MIDIIntent::Continuous;
		queue.enqueue_message(cc);
	}
	uint8_t const sysex[] = {0xF0, 0x7D, 0x01, 0xF7};
	CHECK_TRUE(queue.enqueue_sysex(sysex, sizeof(sysex)));

	for (int i = 0; i < 40; i++) {
		queue.consume_queued_messages(48000 + i * 48000);
	}

	auto const& sent = MidiTransportMock::sent_bytes();
	bool saw_sysex_start = false;
	for (uint8_t b : sent) {
		if (b == 0xF0) {
			saw_sysex_start = true;
		}
	}
	CHECK_TRUE(saw_sysex_start);
}
