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

#include "tuning.h"
#include <cstring>
#include <math.h>

constexpr double two14 = 0x1.p14;
constexpr double two30 = 0x1.p30;
constexpr double two32 = 0x1.p32;

void Tuning::calculateNote(int noteWithin) {

	double cents = 100 * (noteWithin - referenceNote);
	cents += offsets[noteWithin] / 100.0;
	double frequency = referenceFrequency * pow(2.0, cents / 1200.0);

	double value = frequency / 1378.125; //  44,100 Hz / 32
	value *= two32;
	tuningFrequencyTable[noteWithin] = lround(value);

	value = pow(2.0, noteWithin / 12.0) * two30;
	tuningIntervalTable[noteWithin] = lround(value);
}

void Tuning::calculateAll() {

	for (int i = 0; i < 12; i++) {
		calculateNote(i);
	}
}

int32_t Tuning::noteFrequency(int noteWithin) {

	return tuningFrequencyTable[noteWithin];
}

int32_t Tuning::noteInterval(int noteWithin) {

	return tuningIntervalTable[noteWithin];
}

int32_t Tuning::getReference() {

	return int(referenceFrequency * 10.0);
}

void Tuning::setReference(int32_t scaled) {

	referenceFrequency = scaled / 10.0;
	calculateAll();
}

void Tuning::setCents(int noteWithin, double cents) {
	// noteCents[noteWithin] = cents;
	auto c = lround(cents * 100);
	offsets[noteWithin] = c - 10000 * noteWithin;
	calculateNote(noteWithin);
}

void Tuning::setOffset(int noteWithin, int32_t offset) {

	offsets[noteWithin] = offset;
	// noteCents[noteWithin] = noteWithin * 100.0 + offset;
	calculateNote(noteWithin);
}

void Tuning::setFrequency(int note, double freq) {

	auto nwo = noteWithinOctave(note); // double check ±4

	double semitone;
	double estimate = 12.0 * log2(freq / 440.0) + 69;
	double c = 100.0 * modf(estimate, &semitone);

	auto ds = semitone - note;
	setCents(nwo.noteWithin, 100.0 * (nwo.noteWithin + ds) + c);
}

void Tuning::setFrequency(int note, TuningSysex::frequency_t freq) {

	auto nwo = noteWithinOctave(note); // double check ±4

	int ds = freq.semitone - note;
	double c = TuningSysex::cents(freq);

	setCents(nwo.noteWithin, 100.0 * (nwo.noteWithin + ds) + c);
}

double Tuning::getFrequency(int note) {

	auto nwo = noteWithinOctave(note); // double check ±4
	auto span = double(tuningFrequencyTable[nwo.noteWithin]);
	return pow(2.0, nwo.octave) * (span / two32) * 1378.125;
}

void Tuning::getSysexFrequency(int note, TuningSysex::frequency_t& ret) {

	auto nwo = noteWithinOctave(note); // double check ±4
	double o = offsets[nwo.noteWithin] / 10000.0;
	int cents;
	double semitone;

	double c = modf(o, &semitone);
	if (o < 0)
		o -= 1;
	int d = note + int(o);
	cents = c * 16383;

	ret.semitone = d & 0x7f;
	ret.cents.msb = (cents >> 7) & 0x7f;
	ret.cents.lsb = cents & 0x7f;
}

NoteWithinOctave Tuning::noteWithinOctave(int noteCode) {
	return {
	    .octave = int16_t((noteCode + 10 * kOctaveSize) / kOctaveSize),
	    .noteWithin = int16_t((noteCode + 10 * kOctaveSize) % kOctaveSize),
	};
}

void Tuning::setNextCents(double cents) {

	if (nextNote >= MAX_DIVISIONS) {
		nextNote = 0;
		cents -= 1200.0;
	}
	setCents(nextNote++, cents);
}

void Tuning::setNextRatio(int numerator, int denominator) {

	double c = 1200.0 * log2(numerator / (double)denominator);
	if (nextNote >= MAX_DIVISIONS) {
		nextNote = 0;
		c -= 1200.0;
	}
	setCents(nextNote++, c);
}

void Tuning::setDivisions(int divs) {

	divisions = divs;
	if (divisions > MAX_DIVISIONS) {
		divisions = MAX_DIVISIONS;
	}
}

void Tuning::setName(const char* tuning_name) {
	for (int i = 0; i < 16; i++) {
		name[i] = tuning_name[i] & 0x7f;
		if (tuning_name[i] == '\0')
			break;
	}
}

void Tuning::setup(const char* tuning_name) {

	nextNote = 0;
	setName(tuning_name);
}

Tuning::Tuning() : referenceNote(5), divisions(12), nextNote(0), referenceFrequency(440.0) {
	// noteWithin: 0=E, 2=F#, 4=G#, 5=A=440 Hz
	calculateAll();
}

Tuning::Tuning(Tuning& other)
    : referenceNote(other.referenceNote), divisions(other.divisions), nextNote(0),
      referenceFrequency(other.referenceFrequency) {
	for (int i = 0; i < divisions; i++) {
		setOffset(i, other.offsets[i]);
	}
}

// TuningSystem namespace follows

namespace TuningSystem {
Tuning tunings[NUM_TUNINGS];
Tuning* tuning;
int32_t selectedTuning;
} // namespace TuningSystem

void TuningSystem::initialize() {
	selectedTuning = 0;
	select(0);

	for (int i = 0; i < MAX_DIVISIONS; i++) {
		tuning->setOffset(i, 0);
	}
	tuning->setName("12TET");

	// example usage
	/*
	const int divisions = 12;
	tuning->setDivisions(divisions);
	for (int i = 0; i < divisions; i++) {
	    tuning->setOffset(i, 0);
	}
	tuning->setReference(4400);
	*/
}

void TuningSystem::select(int32_t index) {
	if (index >= NUM_TUNINGS || index < 0) {
		return;
	}
	selectedTuning = index;
	tuning = &(tunings[selectedTuning]);
}

bool TuningSystem::selectForWrite(int32_t index) {
	if (index == 0) {
		// since Tuning 0 is read-only 12TET
		index = 1;
	}
	select(index);
	return true;
}
