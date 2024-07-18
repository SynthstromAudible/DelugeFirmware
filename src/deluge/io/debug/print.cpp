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
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"

extern "C" {
#include "deluge/drivers/uart/uart.h"
}

#define JFTRACE 1
namespace Debug {

constexpr uint32_t kNumSamplesBetweenReports = kSampleRate;
bool initFlag = false;
bool prependDeltaT = true;
bool lastWasNewline = false;

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
uint32_t lutHexString(uint64_t num, char* s) {
	static const char digits[513] = "000102030405060708090A0B0C0D0E0F"
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

	uint32_t x = (uint32_t)num;
	int i = 3;
	char* lut = (char*)digits;
	while (i >= 0) {
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

MIDIDevice* midiDebugDevice = nullptr;

void prependTimeStamp(bool isNewLine) {
#if JFTRACE
	if (!prependDeltaT)
		return;
	if (lastWasNewline) {
		if (!initFlag)
			Debug::init();
		char buffer[32];
		lutHexString(Debug::readCycleCounter(), buffer);
		buffer[8] = ' ';
		buffer[9] = 0;
		if (midiDebugDevice) {
			sysexDebugPrint(midiDebugDevice, buffer, false);
		}
		else {
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

void printsnln(char* label, uint32_t val) {
#if ENABLE_TEXT_OUTPUT
	char buffer[128];
	int len = strlen(label);
	strcpy(buffer, label);
	buffer[len] = ' ';
	intToString(val, buffer + len + 1);
	println(buffer);
#endif
}

RTimer::RTimer(const char* label) : startTime(0), m_label(label), stopped(false) {
#if ENABLE_TEXT_OUTPUT

	readCycleCounter(startTime);
#endif
}

// If stop() or split() hasn't happend yet, stop now.
// Note that the ctor call may be optimized & could occur before block exit.
RTimer::~RTimer() {
#if ENABLE_TEXT_OUTPUT
	if (!stopped) {
		stop();
	}
#endif
}

void RTimer::reset() {
#if ENABLE_TEXT_OUTPUT
	stopped = false;
	readCycleCounter(startTime);
#endif
}

void RTimer::stop() {
#if ENABLE_TEXT_OUTPUT
	uint32_t endTime = 0;
	readCycleCounter(endTime);
	uint32_t deltaT = endTime - startTime;
	stopped = true;
	char buffer[128];
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

// Note: No guarding against buffer overflows!
void RTimer::stop(const char* stopLabel) {
#if ENABLE_TEXT_OUTPUT
	uint32_t endTime = 0;
	readCycleCounter(endTime);
	uint32_t deltaT = endTime - startTime;
	stopped = true;
	char buffer[128];
	lutHexString(startTime, buffer);
	buffer[8] = ',';
	lutHexString(deltaT, buffer + 9);
	buffer[17] = ' ';
	strcpy(buffer + 18, m_label);
	char* stopplace = buffer + 18 + strlen(m_label);
	strcpy(stopplace, stopLabel);
	if (midiDebugDevice) {
		sysexDebugPrint(midiDebugDevice, buffer, true);
	}
	else {
		uartPrintln(buffer);
	}
	startTime = endTime;
#endif
}

Averager::Averager(const char* label, uint32_t repeats) : m_label(label), accumulator(0), N(repeats), c(0) {
}

void Averager::note(int32_t val) {
#if ENABLE_TEXT_OUTPUT
	accumulator += val;
	if (N == 0)
		return;
	c++;
	if (c >= N) {
		int32_t avg = accumulator / c;
		printsnln((char*)m_label, avg);
		accumulator = 0;
		c = 0;
	}
#endif
}

void Averager::setN(uint32_t n) {
	N = n;
}

OneOfN::OneOfN(const char* label, uint32_t repeats) : active(false), N(repeats), c(0), myRTimer(label) {
}

void OneOfN::setN(uint32_t n) {
#if ENABLE_TEXT_OUTPUT
	N = n;
#endif
}

void OneOfN::start() {
#if ENABLE_TEXT_OUTPUT
	if (N == 0)
		return;
	c++;
	if (c > N) {
		active = true;
		myRTimer.reset();
	}
#endif
}

void OneOfN::stop() {
#if ENABLE_TEXT_OUTPUT
	if (N > 0 && active) {
		active = false;
		c = 0;
		myRTimer.stop();
	}
#endif
}

void OneOfN::split(const char* splitLabel) {
#if ENABLE_TEXT_OUTPUT
	if (!active || N == 0)
		return;
	myRTimer.stop(splitLabel);
#endif
}

OnceEvery::OnceEvery(const char* label, uint32_t timeBase) : active(false), timeBase(timeBase), t0(0), myRTimer(label) {
}

void OnceEvery::start() {
#if ENABLE_TEXT_OUTPUT
	uint32_t t1;
	readCycleCounter(t1);
	uint32_t deltaT = t1 - t0;
	if (deltaT >= timeBase) {
		active = true;
		t0 = t1;
		myRTimer.reset();
	}
#endif
}

void OnceEvery::stop() {
#if ENABLE_TEXT_OUTPUT
	if (!active)
		return;
	myRTimer.stop();
	active = false;
#endif
}

void OnceEvery::split(const char* splitLabel) {
#if ENABLE_TEXT_OUTPUT
	if (!active)
		return;
	myRTimer.stop(splitLabel);
#endif
}

CountsPer::CountsPer(const char* label, uint32_t timeBase)
    : label(label), timeBase(timeBase), active(false), count(0), t0(0) {
}

void CountsPer::CountsPer::bump(uint32_t by) {
#if ENABLE_TEXT_OUTPUT
	if (!active) {
		count = 1;
		active = true;
		readCycleCounter(t0);
	}
	else {
		uint32_t t1;
		readCycleCounter(t1);
		uint32_t deltaT = t1 - t0;
		if (deltaT >= timeBase) {
			printsnln((char*)label, count);
			count = by;
			t0 = t1;
		}
		else {
			count += by;
		}
	}
#endif
}

void CountsPer::clear() {
#if ENABLE_TEXT_OUTPUT
	active = false;
	count = 0;
#endif
}

AverageDT::AverageDT(const char* label, uint32_t timeBase, uint32_t scaling)
    : label(label), timeBase(timeBase), active(false), scaling(scaling), accumulator(0), count(0), t0(0), tnm1(0) {
}

void AverageDT::begin() {
#if ENABLE_TEXT_OUTPUT
	readCycleCounter(tnm1);
#endif
}

void AverageDT::note() {
#if ENABLE_TEXT_OUTPUT
	if (!active) {
		count = 0;
		active = true;
		readCycleCounter(t0);
		tnm1 = t0;
		accumulator = 0;
	}
	else {
		uint32_t t1;
		readCycleCounter(t1);
		uint32_t deltaT = t1 - t0;
		if (deltaT >= timeBase) {
			uint32_t avg;
			if (count == 0) {
				avg = deltaT / scaling;
			}
			else {
				avg = accumulator / count;
			}
			if (scaling > 1)
				avg = avg / scaling;
			printsnln((char*)label, avg);
			count = 0;
			accumulator = 0;
			t0 = t1;
		}
		accumulator += (t1 - tnm1);
		count++;
		tnm1 = t1;
	}
#endif
}

void AverageDT::clear() {
#if ENABLE_TEXT_OUTPUT
	active = false;
	count = 0;
	accumulator = 0;
#endif
}

AverageVOT::AverageVOT(const char* label, uint32_t timeBase)
    : label(label), timeBase(timeBase), active(false), accumulator(0), count(0), t0(0) {
}

void AverageVOT::note(uint32_t value) {
#if ENABLE_TEXT_OUTPUT
	if (!active) {
		count = 0;
		accumulator = 0;
		active = true;
		readCycleCounter(t0);
	}
	else {
		uint32_t t1;
		readCycleCounter(t1);
		uint32_t deltaT = t1 - t0;
		if (deltaT >= timeBase) {
			uint32_t avg;
			if (count != 0) {
				avg = (accumulator / count);
				printsnln((char*)label, avg);
			}
			count = 0;
			accumulator = 0;
			t0 = t1;
		}
		accumulator += value;
		count++;
	}
#endif
}

void AverageVOT::clear() {
#if ENABLE_TEXT_OUTPUT
	active = false;
	count = 0;
	accumulator = 0;
#endif
}

} // namespace Debug
