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

//int32_t tuningFrequencyOffsetTable[12];
//int32_t tuningIntervalOffsetTable[12];
int32_t selectedTuningBank;

void TuningSystem::TuningSystem()
{
	setDefaultTuning();
}

void TuningSystem::setDefaultTuning() {

	for(int i = 0; i < 12; i++) {
		tuningIntervalOffsetTable[i] = 0;
		tuningFrequencyOffsetTable[i] = 0;

		fineTuners[i].setNoDetune()
	}
	selectedTuningBank = 0;
}

void TuningSystem::setBank(int bank) {

	if (bank == 0) {
		setDefaultTuning();
	}

	else {
		// TODO load other tunings
		calculateUserTuning();
	}
}

void TuningSystem::calculateOffset(int noteWithinOctave) {

	fineTuners[noteWithinOctave].setup(offsets[noteWithinOctave] * 42949672); // scale by 2^30 div 50
}

void TuningSystem::calculateUserTuning() {

	for(int i = 0; i < 12; i++) {
		calculateOffset(i);
	}

}

void TuningSystem::setOffset(int noteWithinOctave, int32_t offset) {

	offsets[note] = offset;
	calculateOffset(noteWithinOctave);
}

