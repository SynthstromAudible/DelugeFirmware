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

namespace Debug {
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

	void	setCount(uint32_t repeats);
	void	logValue(int32_t val);
	void 	setN(uint32_t numRepeats);

	const char* m_label;
	int64_t accumulator;
	uint32_t N;
	uint32_t c;

};

class OneOfN{
public:
	OneOfN(const char* label, uint32_t repeats = 0);

	void start();
	void stop();

	void split(const char* splitLabel);
	void setN(uint32_t numRepeats);

	bool		active;
	uint32_t 	repeats;
	uint32_t 	N;
	uint32_t 	c;
	RTimer		myRTimer;
};


extern MIDIDevice* midiDebugDevice;
} // namespace Debug
