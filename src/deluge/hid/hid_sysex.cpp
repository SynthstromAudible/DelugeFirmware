#include "hid/hid_sysex.h"
#include "hid/display/oled.h"
#include "io/midi/midi_engine.h"
#include "util/functions.h"

void HIDSysex::sysexReceived(int ip, int d, int cable, uint8_t* data, int len) {
	if (len < 6) {
		return;
	}
	// first three bytes are already used, next is command
	switch (data[3]) {
	case 0: // request OLED dispaly
		requestOLEDDisplay(ip, d, cable, data, len);
		break;

	default:
		break;
	}
}

void HIDSysex::requestOLEDDisplay(int ip, int d, int cable, uint8_t* data, int len) {
	if (data[4] == 0 or data[4] == 1) {
		sendOLEDData(ip, d, cable, (data[4] == 1));
	}
}

static uint8_t big_buffer[1024];

void HIDSysex::sendOLEDData(int ip, int d, int cable, bool rle) {
	// TODO: in the long run, this should not depend on having a physical OLED screen
#if HAVE_OLED
	const int block_size = 768;
	const int packed_block_size = 878;

	rle = false; // not yet implemented

	uint8_t reply_hdr[5] = {0xf0, 0x7e, 0x02, 0x40, rle ? 0x02 : 0x01};
	uint8_t* reply = big_buffer;
	memcpy(reply, reply_hdr, 5);
	reply[5] = 0; // nominally 32*data[5] is start pos for a delta

	int packed = pack_8bit_to_7bit(reply + 6, packed_block_size, OLED::oledCurrentImage[0], block_size);
	if (packed != packed_block_size) {
		OLED::popupText("eror: fail");
	}
	reply[6 + packed_block_size] = 0xf7; // end of transmission
	midiEngine.sendSysex(ip, d, cable, reply, packed_block_size + 7);
#endif
}
