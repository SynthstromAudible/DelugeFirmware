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

#pragma once

#include <cstdint>

class MIDIDevice;
#include "definitions_cxx.hpp"

namespace Debug {

void sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
void sysexDebugPrint(MIDIDevice* device, const char* msg, bool nl);
#ifdef ENABLE_SYSEX_LOAD
void loadPacketReceived(uint8_t* data, int32_t len);
void loadCheckAndRun(uint8_t* data, int32_t len);
#endif

} // namespace Debug

namespace SysEx {

const uint8_t SYSEX_START = 0xF0;
const uint8_t DELUGE_SYSEX_ID_BYTE0 = 0x00;
const uint8_t DELUGE_SYSEX_ID_BYTE1 = 0x21;
const uint8_t DELUGE_SYSEX_ID_BYTE2 = 0x7B;
const uint8_t DELUGE_SYSEX_ID_BYTE3 = 0x01;

const uint8_t SYSEX_UNIVERSAL_NONRT = 0x7E;
const uint8_t SYSEX_UNIVERSAL_RT = 0x7F;
const uint8_t SYSEX_UNIVERSAL_IDENTITY = 0x06;

const uint8_t SYSEX_END = 0xF7;

enum SysexCommands : uint8_t {
	Ping,       // reply with pong
	Popup,      // display info in popup
	HID,        // HID access
	Debug,      // Debugging
	Json,       // Json Request
	JsonReply,  // Json Response
	Pong = 0x7F // Pong reply
};

} // namespace SysEx
