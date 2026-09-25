#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue_manager.h"
#include <cstring>

// Drives the real USB drain path: enqueue packed USB-MIDI events, then assemble a transfer the way
// ConnectedUSBMIDIDevice does. The DIN counterparts live in midi_din_drain_tests.cpp; both transports
// share one CC-lane policy, so these pin the USB half of it - coalescing, debt reordering, and the fact
// that only the value byte of a packed event is ever rewritten.

namespace {
/// Packs a channel CC the way ConnectedUSBMIDIDevice does: byte0 cable/CIN, then status, CC, value.
uint32_t pack_usb_cc(uint8_t channel, uint8_t cc, uint8_t value) {
	return (static_cast<uint32_t>(value) << 24) | (static_cast<uint32_t>(cc) << 16)
	       | (static_cast<uint32_t>(0xB0 | channel) << 8) | 0x0B;
}
} // namespace

TEST_GROUP(MIDIUsbDrain){};

TEST(MIDIUsbDrain, ContinuousCCsAreCoalescedThenOrderedByDebt) {
	MIDIQueueManagerUSB queue;
	queue.reset_queue_storage();

	// CC40 is swept while queued; CC10 and CC25 are each queued once, after it.
	for (uint8_t v = 0; v < 5; v++) {
		(void)queue.enqueue_message(pack_usb_cc(0, 40, v), MIDIIntent::Continuous);
	}
	(void)queue.enqueue_message(pack_usb_cc(0, 10, 7), MIDIIntent::Continuous);
	(void)queue.enqueue_message(pack_usb_cc(0, 25, 9), MIDIIntent::Continuous);

	uint8_t transfer[256] = {0};
	uint8_t num_bytes = 0;
	CHECK_TRUE(queue.consume_queued_messages(transfer, num_bytes, false));
	CHECK_EQUAL(12, num_bytes); // three events: the five CC40 updates collapsed into one

	uint32_t sent[3] = {0, 0, 0};
	memcpy(sent, transfer, sizeof(sent));
	// The coalesced CC carries debt, so it is emitted first and carries the newest value.
	CHECK_EQUAL(40, static_cast<uint8_t>(sent[0] >> 16));
	CHECK_EQUAL(4, static_cast<uint8_t>(sent[0] >> 24));
	CHECK_EQUAL(0x0B, static_cast<uint8_t>(sent[0] & 0xFF)); // cable/CIN survives the rewrite
	// The rest fall back to round-robin by CC number.
	CHECK_EQUAL(10, static_cast<uint8_t>(sent[1] >> 16));
	CHECK_EQUAL(25, static_cast<uint8_t>(sent[2] >> 16));
	CHECK_FALSE(queue.has_buffered_send_data());
}

TEST(MIDIUsbDrain, EventCCsKeepTheirOrderAndTheirDuplicateValues) {
	// The RPN case over USB: Event intent keeps these off the coalescing/reordering lane entirely.
	MIDIQueueManagerUSB queue;
	queue.reset_queue_storage();

	uint8_t const ccs[][2] = {{100, 6}, {101, 0}, {6, 4}, {100, 127}, {101, 127}};
	for (auto const& c : ccs) {
		(void)queue.enqueue_message(pack_usb_cc(0, c[0], c[1]), MIDIIntent::Event);
	}

	uint8_t transfer[256] = {0};
	uint8_t num_bytes = 0;
	CHECK_TRUE(queue.consume_queued_messages(transfer, num_bytes, false));
	CHECK_EQUAL(20, num_bytes); // five events, none merged

	for (int i = 0; i < 5; i++) {
		uint32_t sent = 0;
		memcpy(&sent, transfer + (i * 4), sizeof(sent));
		CHECK_EQUAL(ccs[i][0], static_cast<uint8_t>(sent >> 16));
		CHECK_EQUAL(ccs[i][1], static_cast<uint8_t>(sent >> 24));
	}
}
