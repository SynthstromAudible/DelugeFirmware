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

#include "io/debug/print.h"
#include "io/debug/sysex.h"
#include "io/midi/midi_engine.h"
#include "util/functions.h"
#include <math.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

namespace Debug {

constexpr uint32_t kNumSamplesBetweenReports = 44100;

bool	 initFlag = false;
bool	 prependDeltaT = true;
bool	 lastWasNewline = false;

[[gnu::always_inline]] inline void ResetClock() {
	// Reset the PMU so we can read out a time later.
	// See DDI0406C.d, page B4-1672 (PMCR, Performance Monitors Control Register, VMSA)
	//  - bit 4 [1] Enable export of the PMU events during halt
	//  - bit 3 [0] No cycle count divider
	//  - bit 2 [1] Cycle counter reset
	//  - bit 1 [1] Event counter reset
	//  - bit 0 [1] Enable all counters.
	uint32_t const pmcr = 0b10111;
	asm volatile("MCR p15, 0, %0, c9, c12, 0\n" : : "r"(pmcr));
}

// https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/
uint32_t lutHexString(uint64_t num, char *s)
{
	static const char digits[513] =
		"000102030405060708090A0B0C0D0E0F"
		"101112131415161718191A1B1C1D1E1F"
		"202122232425262728292A2B2C2D2E2F"
		"303132333435363738393A3B3C3D3E3F"
		"404142434445464748494A4B4C4D4E4F"
		"505152535455565758595A5B5C5D5E5F"
		"606162636465666768696A6B6C6D6E6F"
		"707172737475767778797A7B7C7D7E7F"
		"808182838485868788898A8B8C8D8E8F"
		"909192939495969798999A9B9C9D9E9F"
		"A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"
		"B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"
		"C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF"
		"D0D1D2D3D4D5D6D7D8D9DADBDCDDDEDF"
		"E0E1E2E3E4E5E6E7E8E9EAEBECEDEEEF"
		"F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF";

	uint32_t x =(uint32_t) num;
	int i = 3;
	char *lut = (char *)digits;
	while (i >= 0)
	{
		int pos = (x & 0xFF) * 2;
		char ch = lut[pos];
		s[i * 2] = ch;

		ch = lut[pos + 1];
		s[i * 2 + 1] = ch;

		x >>= 8;
		i -= 1;
	}

	return 0;
}

void init() {
	// Enable the PMU cycle counter
	//
	// See ARM DDI 0406C.d, page B4-1671 (PMCR, Performance Monitors Control Register, VMSA)
	uint32_t const pmcr = 0;
	uint32_t const pmcntenset = 0b10000000000000000000000000000000u;

	asm volatile("MRC p15, 0, %0, c9, c12, 0\n"
	             // Set bit 0, the "E" flag
	             "orr %0, #1\n"
	             "MCR p15, 0, %0, c9, c12, 0\n"
	             "MCR p15, 0, %1, c9, c12, 1\n"
	             :
	             : "r"(pmcr), "r"(pmcntenset));

	initFlag = true;
}

[[gnu::always_inline]] inline uint32_t sampleCycleCounter() {
		uint32_t cycles = 0;
		asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(cycles) :);
		return cycles;
}


MIDIDevice* midiDebugDevice = nullptr;


void prependTimeStamp(bool isNewLine)
{
#if ENABLE_TEXT_OUTPUT
	if (!prependDeltaT) return;
	if (lastWasNewline) {
		if (!initFlag) Debug::init();
		char buffer[32];
		//intToString(Debug::sampleCycleCounter(), buffer);
		lutHexString(Debug::sampleCycleCounter(), buffer);
		buffer[8] = ' ';
		buffer[9] = 0;
		if (midiDebugDevice) {
			sysexDebugPrint(midiDebugDevice, buffer, false);
		} else
		{
			uartPrint(buffer);
		}
	}
	lastWasNewline = isNewLine;
#endif
}

void println(char const* output) {
#if ENABLE_TEXT_OUTPUT
	prependTimeStamp(true);
	if (midiDebugDevice) {
		sysexDebugPrint(midiDebugDevice, output, true);
	}
	else {
		uartPrintln(output);
	}
#endif
}

void println(int32_t number) {
#if ENABLE_TEXT_OUTPUT
	char buffer[12];
	intToString(number, buffer);
	println(buffer);
#endif
}

void print(char const* output) {
#if ENABLE_TEXT_OUTPUT
	prependTimeStamp(false);
	if (midiDebugDevice) {
		sysexDebugPrint(midiDebugDevice, output, false);
	}
	else {
		uartPrint(output);
	}
#endif
}

void print(int32_t number) {
#if ENABLE_TEXT_OUTPUT
	char buffer[12];
	intToString(number, buffer);
	print(buffer);
#endif
}


RoutineTimer::RoutineTimer(const char* label) :
	startTime(0),
	m_label(label) {
#if ENABLE_TEXT_OUTPUT
	asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(startTime) :);
#endif
}

void RoutineTimer::split(const char* splitLabel) {
#if ENABLE_TEXT_OUTPUT
	uint32_t endTime = 0;
	asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(endTime) :);
	uint32_t deltaT = endTime - startTime;
	char buffer[128];
	lutHexString(endTime, buffer);
	buffer[8] = ',';
	lutHexString(deltaT, buffer + 9);
	buffer[17] = ' ';
	strcpy(buffer + 18, m_label);
	char* splitplace = buffer + 18 + strlen(m_label);
	*splitplace++ = ':';
	strcpy(splitplace, splitLabel);
	if (midiDebugDevice) {
		sysexDebugPrint(midiDebugDevice, buffer, true);
	}
	else {
		uartPrintln(buffer);
	}
#endif
}


void RoutineTimer::stop() {
#if ENABLE_TEXT_OUTPUT
	uint32_t endTime = 0;
	asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(endTime) :);
	uint32_t deltaT = endTime - startTime;
	char buffer[64];
	lutHexString(startTime, buffer);
	buffer[8] = ',';
	lutHexString(deltaT, buffer + 9);
	buffer[17] = ' ';
	strcpy(buffer + 18, m_label);
	if (midiDebugDevice) {
		sysexDebugPrint(midiDebugDevice, buffer, true);
	}
	else {
		uartPrintln(buffer);
	}
#endif
}


} // namespace Debug

