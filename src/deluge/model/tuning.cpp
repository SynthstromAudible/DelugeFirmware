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
#include <math.h>

int32_t tuningFrequencyTable[12];
int32_t tuningIntervalTable[12];

TuningSystem tuningSystem;

int32_t selectedTuningBank;

void TuningSystem::calculateNote(int noteWithinOctave) {

	double cents = 100 * (noteWithinOctave - referenceNote);
	cents += offsets[noteWithinOctave] / 100.0;
	double frequency = referenceFrequency * pow(2.0, cents / 1200.0);

	double value = frequency / 1378.125;
	value *= pow(2.0, 32);
	tuningFrequencyTable[noteWithinOctave] = lround(value);

	value = pow(2.0, noteWithinOctave / 12.0) * pow(2.0, 30);
	tuningIntervalTable[noteWithinOctave] = lround(value);
}

void TuningSystem::calculateAll() {

	for (int i = 0; i < 12; i++) {
		calculateNote(i);
	}
}

int32_t TuningSystem::noteFrequency(int noteWithinOctave) {

	return tuningFrequencyTable[noteWithinOctave];
}

int32_t TuningSystem::noteInterval(int noteWithinOctave) {

	return tuningIntervalTable[noteWithinOctave];
}

int32_t TuningSystem::getReference() {

	return int(referenceFrequency * 10.0);
}

void TuningSystem::setReference(int32_t scaled) {

	referenceFrequency = scaled / 10.0;
	calculateAll();
}

void TuningSystem::setNoteCents(int noteWithinOctave, double cents) {
	noteCents[noteWithinOctave] = cents;
	offsets[noteWithinOctave] = cents - 100.0 * noteWithinOctave;
}

void TuningSystem::setOffset(int noteWithinOctave, int32_t offset) {

	offsets[noteWithinOctave] = offset;
	noteCents[noteWithinOctave] = noteWithinOctave * 100.0 + offset;
	calculateNote(noteWithinOctave);
}

void TuningSystem::setBank(int bank) {

	if (bank == 0) {
		setDefaultTuning();
	}

	else {
		// TODO load other tunings
		calculateAll();
	}
}

void TuningSystem::setNextCents(double cents) {

	int note = nextNote++;
	noteCents[note] = cents;
	offsets[note] = cents - 100.0 * note;
}

void TuningSystem::setNextRatio(int numerator, int denominator) {

	int note = nextNote++;
	noteCents[note] = 1200.0 * log2(numerator / (double)denominator);
	offsets[note] = noteCents[note] - 100.0 * note;
}

void TuningSystem::setDivisions(int divs) {

	divisions = divs;
	if (divisions > MAX_DIVISIONS) {
		divisions = MAX_DIVISIONS;
		// TODO flash a warning
	}
}

void TuningSystem::setup(const char* description) {

	nextNote = 0;
}

void TuningSystem::setDefaultTuning() {

	selectedTuningBank = 0;
	setDivisions(12);
	for (int i = 0; i < divisions; i++) {
		setOffset(i, 0);
	}
	setReference(4400);
}

TuningSystem::TuningSystem() {
	referenceNote = 5; // noteWithinOctave: 0=E, 2=F#, 4=G#, 5=A=440 Hz
	setDefaultTuning();
}
