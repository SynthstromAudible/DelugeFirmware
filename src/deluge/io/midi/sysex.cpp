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
#include "hid/display/screensaver.h"
#include "io/debug/log.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/alloc_metadata.h"
#include "storage/flash_storage.h"

#include "util/pack.h"

#if MEM_GUARD
// Baseline for the leak snapshot/diff workflow (Debug sysex subcommand 3). Persists between sysex calls so a developer
// can snapshot, drive the suspect operation, then ask for everything that leaked since.
namespace {
mem_guard::Snapshot g_leakSnapshot = 0;
}
#endif

void Debug::sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}

	// Subcommand is second byte of payload
	switch (data[1]) {
	case 0:
		if (data[2] == 1) {
			midiDebugCable = &cable;
		}
		else if (data[2] == 0) {
			midiDebugCable = nullptr;
		}
		break;

	case 3:
		// Leak hunting (MEM_GUARD builds only). data[2]: 0 = take snapshot baseline, 1 = report what leaked since the
		// last snapshot, 2 = report ALL live allocations. Output goes to the debug log (D_PRINTLN), same channel as the
		// rest of MEM_GUARD's reporting.
#if MEM_GUARD
		switch (data[2]) {
		case 0:
			g_leakSnapshot = mem_guard::snapshot();
			D_PRINTLN("MEM_GUARD leak snapshot taken at epoch %d (%d live)", g_leakSnapshot,
			          mem_guard::liveAllocations());
			break;
		case 1:
			mem_guard::reportOutstanding(g_leakSnapshot);
			break;
		case 2:
			mem_guard::reportOutstanding(0);
			break;
		default:
			break;
		}
#endif
		break;

	default:
		break;
	}
}

void Debug::sysexDebugPrint(MIDICable& cable, const char* msg, bool nl) {
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
	cable.sendSysex(reply, len + sizeof(reply_hdr) + 1);
}
