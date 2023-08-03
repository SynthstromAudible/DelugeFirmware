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
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "util/functions.h"

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
