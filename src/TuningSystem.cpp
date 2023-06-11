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

#include <math.h>
#include "TuningSystem.h"

int32_t tuningFrequencyTable[12];
int32_t tuningIntervalTable[12];

TuningSystem tuningSystem;

int32_t selectedTuningBank;

const double sampleRateDiv4 = 11025.0;

void TuningSystem::calculateOffset(int noteWithinOctave) {

	double cents = 100 * (noteWithinOctave - 5);
	cents += offsets[noteWithinOctave] / 100.0;
	double frequency = referenceFrequency * pow(2.0, cents / 1200.0);


	double value = frequency / 1378.125;
	value *= pow(2.0, 32);
	tuningFrequencyTable[noteWithinOctave] = lround(value);

	value = pow(2.0, noteWithinOctave / 12.0) * pow(2.0, 30);
	tuningIntervalTable[noteWithinOctave] = lround(value);
}

void TuningSystem::calculateAll() {

	for(int i = 0; i < 12; i++) {
		calculateOffset(i);
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

void TuningSystem::setOffset(int noteWithinOctave, int32_t offset) {

	offsets[noteWithinOctave] = offset;
	calculateOffset(noteWithinOctave);
}

void TuningSystem::setDefaultTuning() {

	selectedTuningBank = 0;
	setReference(4400);
	for(int i = 0; i < 12; i++) {
		setOffset(i, 0);
	}
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

TuningSystem::TuningSystem()
{
	setDefaultTuning();
}

