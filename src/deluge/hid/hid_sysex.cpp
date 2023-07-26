#include "hid/hid_sysex.h"
#include "hid/display/oled.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "util/pack.h"
#include <cstring>

void HIDSysex::sysexReceived(MIDIDevice* device, uint8_t* data, int len) {
	if (len < 6) {
		return;
	}
	// first three bytes are already used, next is command
	switch (data[3]) {
	case 0:
		requestOLEDDisplay(device, data, len);
		break;

	case 1:
		request7SegDisplay(device, data, len);
		break;

	default:
		break;
	}
}

void HIDSysex::requestOLEDDisplay(MIDIDevice* device, uint8_t* data, int len) {
	if (data[4] == 0 or data[4] == 1) {
		sendOLEDData(device, (data[4] == 1));
	}
}

void HIDSysex::sendOLEDData(MIDIDevice* device, bool rle) {
	// TODO: in the long run, this should not depend on having a physical OLED screen
#if HAVE_OLED
	const int data_size = 768;
	const int max_packed_size = 922;

	uint8_t reply_hdr[5] = {0xf0, 0x7d, 0x02, 0x40, rle ? 0x01 : 0x00};
	uint8_t* reply = midiEngine.sysex_fmt_buffer;
	memcpy(reply, reply_hdr, 5);
	reply[5] = 0; // nominally 32*data[5] is start pos for a delta

	int packed;
	if (rle) {
		packed = pack_8to7_rle(reply + 6, max_packed_size, OLED::oledCurrentImage[0], data_size);
	}
	else {
		packed = pack_8bit_to_7bit(reply + 6, max_packed_size, OLED::oledCurrentImage[0], data_size);
	}
	if (packed < 0) {
		OLED::popupText("eror: fail");
	}
	reply[6 + packed] = 0xf7; // end of transmission
	device->sendSysex(reply, packed + 7);
#endif
}

void HIDSysex::request7SegDisplay(MIDIDevice* device, uint8_t* data, int len) {
#if !HAVE_OLED
	if (data[4] == 0) {
		// aschually 8 segments if you count the dot
		const int data_size = 4;
		const int packed_data_size = 5;
		uint8_t reply[11] = {0xf0, 0x7d, 0x02, 0x41, 0x00};
		pack_8bit_to_7bit(reply + 6, packed_data_size, numericDriver.lastDisplay, data_size);
		reply[6 + packed_data_size] = 0xf7; // end of transmission
		device->sendSysex(reply, packed_data_size + 7);
	}
#endif
}
