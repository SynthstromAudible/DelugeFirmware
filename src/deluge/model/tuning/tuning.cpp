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

void Tuning::calculateNote(int noteWithinOctave) {

	double cents = 100 * (noteWithinOctave - referenceNote);
	cents += offsets[noteWithinOctave] / 100.0;
	double frequency = referenceFrequency * pow(2.0, cents / 1200.0);

	double value = frequency / 1378.125;
	value *= pow(2.0, 32);
	tuningFrequencyTable[noteWithinOctave] = lround(value);

	value = pow(2.0, noteWithinOctave / 12.0) * pow(2.0, 30);
	tuningIntervalTable[noteWithinOctave] = lround(value);
}

void Tuning::calculateAll() {

	for (int i = 0; i < 12; i++) {
		calculateNote(i);
	}
}

int32_t Tuning::noteFrequency(int noteWithinOctave) {

	return tuningFrequencyTable[noteWithinOctave];
}

int32_t Tuning::noteInterval(int noteWithinOctave) {

	return tuningIntervalTable[noteWithinOctave];
}

int32_t Tuning::getReference() {

	return int(referenceFrequency * 10.0);
}

void Tuning::setReference(int32_t scaled) {

	referenceFrequency = scaled / 10.0;
	calculateAll();
}

void Tuning::setNoteCents(int noteWithinOctave, double cents) {
	noteCents[noteWithinOctave] = cents;
	offsets[noteWithinOctave] = cents - 100.0 * noteWithinOctave;
	calculateNote(noteWithinOctave);
}

void Tuning::setOffset(int noteWithinOctave, int32_t offset) {

	offsets[noteWithinOctave] = offset;
	noteCents[noteWithinOctave] = noteWithinOctave * 100.0 + offset;
	calculateNote(noteWithinOctave);
}

void Tuning::setNextCents(double cents) {

	if (nextNote >= MAX_DIVISIONS)
		return;
	setNoteCents(nextNote++, cents);
}

void Tuning::setNextRatio(int numerator, int denominator) {

	if (nextNote >= MAX_DIVISIONS)
		return;
	setNoteCents(nextNote++, 1200.0 * log2(numerator / (double)denominator));
}

void Tuning::setDivisions(int divs) {

	divisions = divs;
	if (divisions > MAX_DIVISIONS) {
		divisions = MAX_DIVISIONS;
	}
}

void Tuning::setup(const char* description) {

	nextNote = 0;
	strncpy(name, description, 16);
}

Tuning::Tuning() : referenceNote(5), divisions(12), nextNote(0), referenceFrequency(440.0) {
	// noteWithinOctave: 0=E, 2=F#, 4=G#, 5=A=440 Hz
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
