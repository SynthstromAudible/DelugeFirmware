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

class MIDICable;
#include "definitions_cxx.hpp"

namespace Debug {

void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);
void sysexDebugPrint(MIDICable& cable, const char* msg, bool nl);
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
const uint8_t SYSEX_MIDI_TUNING_STANDARD = 0x08;

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

// e.g. F0 7E 08 03 bb tt F7
// SYSEX_START, UNIVERSAL_NONRT, TUNING, bank, preset, SYSEX_END
enum TuningCommands : uint8_t {
	BulkDump = 0x00,       // preset                                // BULK TUNING DUMP REQUEST
	BulkDumpReply,         // preset name[16] {xx yy zz}[128]       // BULK TUNING DUMP
	NoteChange,            // preset len {key xx yy zz}[len]        // SINGLE NOTE TUNING CHANGE (REAL-TIME)
	BankDump,              // bank preset                           // BULK TUNING DUMP REQUEST (BANK)
	KeyBasedDumpReply,     // bank preset name[16] {xx yy zz}[128]  // KEY-BASED TUNING DUMP
	ScaleOctaveDumpReply1, // bank preset name[16] ss[12] csum      // SCALE/OCTAVE TUNING DUMP, 1 byte format
	ScaleOctaveDumpReply2, // bank preset name[16] {ss tt}[12] csum // SCALE/OCTAVE TUNING DUMP, 2 byte format
	BankNoteChange, // bank preset len {key xx yy zz}[len]   // SINGLE NOTE TUNING CHANGE (REAL-TIME / NON REAL-TIME)
	                // (BANK)
	ScaleOctave1,   // ff gg hh ss[12]                       // SCALE/OCTAVE TUNING 1-BYTE FORM (REAL-TIME / NON
	                // REAL-TIME)
	ScaleOctave2 // ff gg hh {ss tt}[12]                  // SCALE/OCTAVE TUNING 2-BYTE FORM (REAL-TIME / NON REAL-TIME)
};
/*
xx yy zz : absolute frequency in Hz. xx=semitone, yyzz=(100/2^14) cents
key      : MIDI key number
len      : length / number of changes
name     : 7-bit ASCII bytes
ff gg hh : channel mask as 00000ff 0ggggggg 0hhhhhhh 16-15, 14-8, 7-1
ss       : relative cents.  -64 to +63, integer step, 0x40 represents equal temperament
ss tt    : relative cents. -100 to +100, fractional step (100/2^13), 0x40 0x00 represents equal temperament
csum     : checksum. can be ignored by receiver
*/

} // namespace SysEx
