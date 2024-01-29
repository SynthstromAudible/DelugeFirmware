/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "io/debug/sysex.h"
#include "gui/l10n/l10n.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/chainload.h"
#include "util/functions.h"
#include "util/pack.h"

extern "C" {
#include "RZA1/oled/oled_low_level.h"
}

void Debug::sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len) {
	if (len < 6) {
		return;
	}

	// first three bytes are already used, next is command
	switch (data[3]) {
	case 0:
		if (data[4] == 1) {
			midiDebugDevice = device;
		}
		else if (data[4] == 0) {
			midiDebugDevice = nullptr;
		}
		break;

	case 1:
#ifdef ENABLE_SYSEX_LOAD
		loadPacketReceived(data, len);
#endif
		break;

	case 2:
#ifdef ENABLE_SYSEX_LOAD
		loadCheckAndRun(data, len);
#endif
		break;

	default:
		break;
	}
}

void Debug::sysexDebugPrint(MIDIDevice* device, const char* msg, bool nl) {
	if (!msg) {
		return; // Do not do that
	}
	// data[4]: reserved, could serve as a message identifier to filter messages per category
	uint8_t reply_hdr[5] = {0xf0, 0x7d, 0x03, 0x40, 0x00};
	uint8_t* reply = midiEngine.sysex_fmt_buffer;
	memcpy(reply, reply_hdr, 5);
	size_t len = strlen(msg);
	len = std::min(len, sizeof(midiEngine.sysex_fmt_buffer) - 7);
	memcpy(reply + 5, msg, len);
	for (int32_t i = 0; i < len; i++) {
		reply[5 + i] &= 0x7F; // only ascii debug messages
	}
	if (nl) {
		reply[5 + len] = '\n';
		len++;
	}

	reply[5 + len] = 0xf7;

	device->sendSysex(reply, len + 6);
}

#ifdef ENABLE_SYSEX_LOAD
static uint8_t* load_buf;
static size_t load_bufsize;
static size_t load_codesize;

static void firstPacket(uint8_t* data, int32_t len) {
	uint8_t tmpbuf[0x40] __attribute__((aligned(CACHE_LINE_SIZE)));
	unpack_7bit_to_8bit(tmpbuf, 0x40, data + 11, 0x4a);
	uint32_t user_code_start = *(uint32_t*)(tmpbuf + OFF_USER_CODE_START);
	uint32_t user_code_end = *(uint32_t*)(tmpbuf + OFF_USER_CODE_END);
	load_codesize = (int32_t)(user_code_end - user_code_start);
	if (load_bufsize < load_codesize) {
		if (load_buf != nullptr) {
			delugeDealloc(load_buf);
		}
		load_bufsize = load_codesize + (511 - ((load_codesize - 1) & 511));

		load_buf = (uint8_t*)GeneralMemoryAllocator::get().allocMaxSpeed(load_bufsize);
		if (load_buf == nullptr) {
			// fail :(
			return;
		}
	}
	PadLEDs::clearAllPadsWithoutSending();
	deluge::hid::display::OLED::clearMainImage();
	deluge::hid::display::OLED::sendMainImage();
}

void Debug::loadPacketReceived(uint8_t* data, int32_t len) {
	uint32_t handshake = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed);
	if (handshake == 0) {
		return; // not allowed
	}

	const int size = 512;
	const int packed_size = 586; // ceil(512+512/7)
	if (len < packed_size + 12) {
		return;
	}

	uint32_t handshake_received;
	unpack_7bit_to_8bit((uint8_t*)&handshake_received, 4, data + 4, 5);
	if (handshake != handshake_received) {
		return;
	}

	int pos = 512 * (data[9] + 0x80 * data[10]);

	if (pos == 0) {
		firstPacket(data, len);
	}

	if (load_buf == nullptr || pos + 512 > load_bufsize) {
		return;
	}

	unpack_7bit_to_8bit(load_buf + pos, size, data + 11, packed_size);

	uint32_t pad = (18 * 8 * pos) / load_bufsize;
	uint8_t col = pad % 18;
	uint8_t row = pad / 18;
	PadLEDs::image[row][col][0] = (255 / 7) * row;
	PadLEDs::image[row][col][1] = 0;
	PadLEDs::image[row][col][2] = 255 - (255 / 7) * row;
	if ((pos / 512) % 16 == 0) {
		PadLEDs::sendOutMainPadColours();
		PadLEDs::sendOutSidebarColours();
	}
}

void Debug::loadCheckAndRun(uint8_t* data, int32_t len) {
	uint32_t handshake = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed);
	if (handshake == 0) {
		return; // not allowed
	}

	if (len < 19 || load_buf == nullptr) {
		return; // cannot do that
	}

	uint32_t fields[3];
	unpack_7bit_to_8bit((uint8_t*)fields, sizeof(fields), data + 4, 14);

	if (handshake != fields[0]) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_BAD_KEY));
		return;
	}

	if (load_codesize != fields[1]) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_WRONG_SIZE));
		return;
	}

	uint32_t checksum = get_crc(load_buf, load_codesize);

	if (checksum != fields[2]) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHECKSUM_FAIL));
		return;
	}

	chainload_from_buf(load_buf, load_codesize);
}
#endif
