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

#ifndef SYSEX_LOGGING_ENABLED
#define SYSEX_LOGGING_ENABLED true
#endif

class MIDIDevice;

namespace Debug {
const uint32_t sec = 400000000;
const uint32_t mS = 400000;
const uint32_t uS = 400;

[[gnu::always_inline]] inline uint32_t readCycleCounter() {
	uint32_t cycles = 0;
	asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(cycles) :);
	return cycles;
}

[[gnu::always_inline]] inline void readCycleCounter(uint32_t& time) {
	asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(time) :);
}

void init();
void print(char const* output);
void println(char const* output);
void println(int32_t number);
void printlnfloat(float number);
void printfloat(float number);
void print(int32_t number);
void ResetClock();

class RTimer {
public:
	RTimer(const char* label);
	~RTimer();

	virtual void reset();
	virtual void stop();
	virtual void stop(const char* stopLabel);

	uint32_t startTime;
	const char* m_label;
	bool stopped;
};

class Averager {
public:
	Averager(const char* label, uint32_t repeats = 0);

	void setCount(uint32_t repeats);
	void note(int32_t val);
	void setN(uint32_t numRepeats);

	const char* m_label;
	int64_t accumulator;
	uint32_t N;
	uint32_t c;
};

class OneOfN {
public:
	OneOfN(const char* label, uint32_t repeats = 0);

	void start();
	void stop();

	void split(const char* splitLabel);
	void setN(uint32_t numRepeats);

	bool active;
	uint32_t N;
	uint32_t c;
	RTimer myRTimer;
};

class OnceEvery {
public:
	OnceEvery(const char* label, uint32_t timeBase);

	void start();
	void stop();

	void split(const char* splitLabel);

	bool active;
	uint32_t timeBase;
	uint32_t t0;
	RTimer myRTimer;
};

class CountsPer {
public:
	CountsPer(const char* label, uint32_t timeBase);
	void bump(uint32_t by = 1);
	void clear();
	const char* label;
	uint32_t timeBase;
	bool active;
	uint32_t count;
	uint32_t t0;
};

class AverageDT {
public:
	AverageDT(const char* label, uint32_t timeBase, uint32_t scaling = 1);
	void begin();
	void note();
	void clear();
	const char* label;
	uint32_t timeBase;
	bool active;
	uint32_t scaling;
	int64_t accumulator;
	uint32_t count;
	uint32_t t0;
	uint32_t tnm1;
};

class AverageVOT {
	AverageVOT(const char* label, uint32_t timeBase);
	void note(uint32_t value);
	void clear();
	const char* label;
	uint32_t timeBase;
	bool active;
	int64_t accumulator;
	uint32_t count;
	uint32_t t0;
};

extern MIDIDevice* midiDebugDevice;
} // namespace Debug
