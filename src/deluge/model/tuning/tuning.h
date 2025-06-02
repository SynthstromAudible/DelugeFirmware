/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "RZA1/system/r_typedefs.h"
#include "types.h"

#define NUM_TUNINGS 10 // must not exceed 127

#define MAX_DIVISIONS 12

class Tuning {
public:
	Tuning();
	Tuning(Tuning& other);
	void setOffset(int, int32_t);

	char name[17];
	int referenceNote; // default 5=A
	double referenceFrequency;
	int32_t offsets[MAX_DIVISIONS]; // cents -5000..+5000
	// int32_t noteCents[MAX_DIVISIONS];

	void setName(const char* name);

	int32_t noteInterval(int);
	int32_t noteFrequency(int);

	int32_t getReference();
	void setReference(int32_t);

	void setCents(int, double);

	void setNextRatio(int, int);
	void setNextCents(double);
	void setup(const char*);
	void setDivisions(int);

	void setFrequency(int note, double freq);
	void setFrequency(int note, TuningSysex::frequency_t freq);

	double getFrequency(int note);
	void getSysexFrequency(int note, TuningSysex::frequency_t& ret);

	NoteWithinOctave noteWithinOctave(int noteCode);

private:
	int32_t tuningFrequencyTable[12];
	int32_t tuningIntervalTable[12];

	void calculateAll();
	void calculateNote(int);

	int nextNote;
	int divisions;
};

namespace TuningSystem {

extern Tuning tunings[NUM_TUNINGS];
extern Tuning* tuning;
extern int32_t selectedTuning;

void initialize();
void select(int32_t);
bool selectForWrite(int32_t);

} // namespace TuningSystem
