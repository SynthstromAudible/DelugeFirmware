#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue_manager.h"
#include <cstring>

// Drives the real USB drain path: enqueue packed USB-MIDI events, then assemble a transfer the way
// ConnectedUSBMIDIDevice does. The DIN counterparts live in midi_din_drain_tests.cpp;

namespace {
/// Packs a channel CC the way ConnectedUSBMIDIDevice does: byte0 cable/CIN, then status, CC, value.
uint32_t pack_usb_cc(uint8_t channel, uint8_t cc, uint8_t value) {
	return (static_cast<uint32_t>(value) << 24) | (static_cast<uint32_t>(cc) << 16)
	       | (static_cast<uint32_t>(0xB0 | channel) << 8) | 0x0B;
}
} // namespace

TEST_GROUP(MIDIUsbDrain){};

TEST(MIDIUsbDrain, EventCCsKeepTheirOrderAndTheirDuplicateValues) {
	// The RPN case over USB: Event intent keeps these off the cc lane entirely.
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
