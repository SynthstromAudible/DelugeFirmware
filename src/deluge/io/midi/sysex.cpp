/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute
 * it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "io/midi/sysex.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "util/chainload.h"

#include "util/pack.h"

void Debug::sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}

	// Subcommand is second byte of payload
	switch (data[1]) {
	case 0:
		if (data[2] == 1) {
			midiDebugDevice = device;
		}
		else if (data[2] == 0) {
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
	// data[4]: reserved, could serve as a message identifier to filter messages
	// per category
	// 	uint8_t reply_hdr[] = {0xf0, 0x7d, 0x03, 0x40, 0x00};
	uint8_t reply_hdr[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x03, 0x40, 0x00};
	uint8_t* reply = midiEngine.sysex_fmt_buffer;
	// memcpy(reply, reply_hdr, 5);
	memcpy(reply, reply_hdr, sizeof(reply_hdr));
	size_t len = strlen(msg);
	//	len = std::min(len, sizeof(midiEngine.sysex_fmt_buffer) - 7);
	len = std::min(len, sizeof(midiEngine.sysex_fmt_buffer) - (sizeof(reply_hdr) + 2));
	//	memcpy(reply + 5, msg, len);
	memcpy(reply + sizeof(reply_hdr), msg, len);
	for (int32_t i = 0; i < len; i++) {
		//		reply[5 + i] &= 0x7F; // only ascii debug messages
		reply[sizeof(reply_hdr) + i] &= 0x7F; // only ascii debug messages
	}
	if (nl) {
		//	reply[5 + len] = '\n';
		reply[sizeof(reply_hdr) + len] = '\n';
		len++;
	}
	//	reply[5 + len] = 0xf7;
	reply[sizeof(reply_hdr) + len] = 0xf7;
	//	device->sendSysex(reply, len + 6);
	device->sendSysex(reply, len + sizeof(reply_hdr) + 1);
}
#ifdef ENABLE_SYSEX_LOAD
#include "gui/l10n/l10n.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/settings/runtime_feature_settings.h"

static uint8_t* load_buf;
static size_t load_bufsize;
static size_t load_codesize;

static void firstPacket(uint8_t* data, int32_t len) {
	uint8_t tmpbuf[0x40] __attribute__((aligned(CACHE_LINE_SIZE)));

	unpack_7bit_to_8bit(tmpbuf, 0x40, data + 9, 0x4a);
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

	// Pad LED Progress Bar Init
	PadLEDs::clearAllPadsWithoutSending();
	PadLEDs::sendOutMainPadColours();
	PadLEDs::sendOutSidebarColours();
	deluge::hid::display::OLED::clearMainImage();
	deluge::hid::display::OLED::sendMainImage();

	boostTask(midiEngine.routine_task_id);
}

void Debug::loadPacketReceived(uint8_t* data, int32_t len) {
	uint32_t handshake = runtimeFeatureSettings.get(RuntimeFeatureSettingType::DevSysexAllowed);
	if (handshake == 0) {
		return; // not allowed
	}

	const int size = 512;
	const int packed_size = 586; // ceil(512+512/7)
	if (len < packed_size + 10) {
		return;
	}

	uint32_t handshake_received;
	unpack_7bit_to_8bit((uint8_t*)&handshake_received, 4, data + 2, 5);
	if (handshake != handshake_received) {
		return;
	}

	int pos = 512 * (data[7] + 0x80 * data[8]);

	if (pos == 0) {
		firstPacket(data, len);
	}

	if (load_buf == nullptr || pos + 512 > load_bufsize) {
		return;
	}

	unpack_7bit_to_8bit(load_buf + pos, size, data + 9, packed_size);

	// Pad LED Progress Bar Step
	uint32_t pad = (18 * 8 * pos) / (load_bufsize - 0xffff);
	uint8_t col = pad % 18;
	uint8_t row = pad / 18;
	PadLEDs::image[row][col] = RGB((255 / 7) * row, 0, 255 - (255 / 7) * row);
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

	if (len < 17 || load_buf == nullptr) {
		return; // cannot do that
	}

	uint32_t fields[3];

	unpack_7bit_to_8bit((uint8_t*)fields, sizeof(fields), data + 2, 14);

	if (handshake != fields[0]) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_BAD_KEY));
		return;
	}

	uint32_t code_file_size = fields[1];

	uint32_t checksum = get_crc(load_buf, code_file_size);

	if (checksum != fields[2]) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHECKSUM_FAIL));
		return;
	}

	chainload_from_buf(load_buf, load_codesize);
}
#endif
