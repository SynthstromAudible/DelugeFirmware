#include "hid/hid_sysex.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "util/pack.h"
#include <cstring>

MIDIDevice* midiDisplayDevice = nullptr;
int32_t midiDisplayUntil = 0;
uint8_t* oledDeltaImage = nullptr;
bool oledDeltaForce = true;

void HIDSysex::sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len) {
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

void HIDSysex::requestOLEDDisplay(MIDIDevice* device, uint8_t* data, int32_t len) {
	if (data[4] == 0 or data[4] == 1) {
		sendOLEDData(device, (data[4] == 1));
	}
	else if (data[4] == 2 || data[4] == 3) {
		bool force = (data[4] == 3);
		midiDisplayDevice = device;
		// two seconds
		midiDisplayUntil = AudioEngine::audioSampleTimer + 2 * 44100;
		if (display->haveOLED()) {
			if (force) {
				oledDeltaForce = true;
			}

			if (oledDeltaImage == nullptr) {
				oledDeltaImage = (uint8_t*)GeneralMemoryAllocator::get().allocMaxSpeed(
				    sizeof(uint8_t[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS]));
			}
		}
		sendDisplayIfChanged();
		if (force && display->have7SEG()) {
			send7SegData(device);
		}
	}
	else if (data[4] == 4) { // SWAP
		deluge::hid::display::swapDisplayType();
		oledDeltaForce = true;
	}
}

void HIDSysex::sendDisplayIfChanged() {
	// NB: timer is only used for throttling, under good conditions sending
	// is driven by the display subsystem only
	uiTimerManager.unsetTimer(TIMER_SYSEX_DISPLAY);
	if (midiDisplayDevice == nullptr || AudioEngine::audioSampleTimer > midiDisplayUntil) {
		return;
	}
	// not exact, but if more than half than the serial buffer is still full,
	// we need to slow down a little. (USB buffer is larger and should be consumed much quicker)
	if (midiDisplayDevice->sendBufferSpace() < 512) {
		uiTimerManager.setTimer(TIMER_SYSEX_DISPLAY, 100);
		return;
	}

	if (display->haveOLED()) {
		sendOLEDDataDelta(midiDisplayDevice, false);
	}
	if (display->have7SEG()) {
		send7SegData(midiDisplayDevice);
	}
}

void HIDSysex::sendOLEDData(MIDIDevice* device, bool rle) {
	if (display->haveOLED()) {
		const int32_t data_size = 768;
		const int32_t max_packed_size = 922;

		uint8_t reply_hdr[5] = {0xf0, 0x7d, 0x02, 0x40, rle ? 0x01_u8 : 0x00_u8};
		uint8_t* reply = midiEngine.sysex_fmt_buffer;
		memcpy(reply, reply_hdr, 5);
		reply[5] = 0; // nominally 32*data[5] is start pos for a delta

		int32_t packed;
		if (rle) {
			packed =
			    pack_8to7_rle(reply + 6, max_packed_size, deluge::hid::display::OLED::oledCurrentImage[0], data_size);
		}
		else {
			packed = pack_8bit_to_7bit(reply + 6, max_packed_size, deluge::hid::display::OLED::oledCurrentImage[0],
			                           data_size);
		}
		if (packed < 0) {
			display->popupTextTemporary("eror: fail");
		}
		reply[6 + packed] = 0xf7; // end of transmission
		device->sendSysex(reply, packed + 7);
	}
}

void HIDSysex::request7SegDisplay(MIDIDevice* device, uint8_t* data, int32_t len) {
	if (data[4] == 0) {
		send7SegData(device);
	}
}

void HIDSysex::send7SegData(MIDIDevice* device) {
	if (display->have7SEG()) {
		// aschually 8 segments if you count the dot
		auto data = display->getLast();
		const int32_t packed_data_size = 5;
		uint8_t reply[12] = {0xf0, 0x7d, 0x02, 0x41, 0x00, 0x00};
		pack_8bit_to_7bit(reply + 6, packed_data_size, data.data(), data.size());
		reply[6 + packed_data_size] = 0xf7; // end of transmission
		device->sendSysex(reply, packed_data_size + 7);
	}
}

void HIDSysex::sendOLEDDataDelta(MIDIDevice* device, bool force) {
	const int32_t data_size = 768;
	const int32_t max_packed_size = 922;

	uint8_t* current = deluge::hid::display::OLED::oledCurrentImage[0];

	int32_t first_change = 9000;
	int32_t last_change = 0;
	int32_t* blkdata_new = (int32_t*)current;
	int32_t* blkdata_old = (int32_t*)oledDeltaImage;

	const int32_t word_size = data_size >> 2;

	if (force || oledDeltaForce) {
		first_change = 0;
		last_change = word_size - 1;
	}
	else {
		for (int32_t blk = 0; blk < word_size; blk++) {
			if (blkdata_new[blk] != blkdata_old[blk]) {
				if (first_change > blk) {
					first_change = blk;
				}
				last_change = blk;
			}
		}
	}

	if (first_change > word_size) {
		return;
	}

	int start = first_change / 2;
	int len = (last_change / 2) - start + 1;

	uint8_t reply_hdr[5] = {0xf0, 0x7d, 0x02, 0x40, 0x02};
	uint8_t* reply = midiEngine.sysex_fmt_buffer;
	memcpy(reply, reply_hdr, 5);
	reply[5] = start;
	reply[6] = len;

	int32_t packed = pack_8to7_rle(reply + 7, max_packed_size, current + 8 * start, 8 * len);
	if (packed <= 0) {
		return;
	}
	memcpy(oledDeltaImage + (8 * start), current + (8 * start), 8 * len);
	oledDeltaForce = false;
	reply[7 + packed] = 0xf7; // end of transmission
	device->sendSysex(reply, packed + 8);
}
