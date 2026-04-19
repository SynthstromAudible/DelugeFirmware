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
#include <cmath>
#include <cstring>

constexpr double two14 = 0x1.p14;
constexpr double two30 = 0x1.p30;
constexpr double two32 = 0x1.p32;
constexpr double kSampleRateDiv32 = kSampleRate / 32.0;
constexpr double kCentsPerOctave = 1200.0;

void Tuning::calculateFrequency(int noteWithin) {
	// MIDI note is C based, shift to E-based for frequency
	int index = (divisions + noteWithin - 4) % divisions;
	double offset = offsets[noteWithin] / 100.0;
	double cents = 100 * (index - 5) + offset; // reference note is A (five semitones above E)
	double frequency = referenceFrequency * std::pow(2.0, cents / kCentsPerOctave);

	double value = frequency / kSampleRateDiv32;
	value *= two32;
	tuningFrequencyTable[index] = lround(value);
}

void Tuning::calculateInterval(int noteWithin) {
	double offset = offsets[noteWithin] / 100.0;
	double cents = 100 * noteWithin + offset;

	double value = std::pow(2.0, cents / kCentsPerOctave);
	value *= two30;
	tuningIntervalTable[noteWithin] = lround(value);
}

void Tuning::calculateNote(int noteWithin) {
	calculateFrequency(noteWithin);
	calculateInterval(noteWithin);
}

void Tuning::calculateAll() {

	for (int i = 0; i < divisions; i++) {
		calculateNote(i);
	}
}

int32_t Tuning::noteFrequency(NoteWithinOctave nwo) {

	auto nwo2 = shiftForFrequency(nwo);
	int32_t shiftRightAmount = 20 - nwo2.octave;
	int32_t freq = tuningFrequencyTable[nwo2.noteWithin];
	if (nwo2.octave > 0) {
		freq >>= shiftRightAmount;
	}
	return freq;
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
	double estimate = 12.0 * log2(freq / 440.0) + 69; // MIDI specifies reference as A=440
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
	return std::pow(2.0, nwo.octave) * (span / two32) * kSampleRateDiv32;
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

inline NoteWithinOctave Tuning::shiftForFrequency(NoteWithinOctave nwo) {
	// MIDI note is C based, shift to E-based for frequency
	if (nwo.noteWithin < 4) {
		return {
		    .octave = int16_t(nwo.octave - 1),
		    .noteWithin = int16_t(nwo.noteWithin + kOctaveSize - 4),
		};
	}
	else {
		return {
		    .octave = int16_t(nwo.octave),
		    .noteWithin = int16_t(nwo.noteWithin - 4),
		};
	}
}

void Tuning::setNextCents(double cents) {

	if (nextNote >= MAX_DIVISIONS) {
		nextNote = 0;
		cents -= kCentsPerOctave;
	}
	setCents(nextNote++, cents);
}

void Tuning::setNextRatio(int numerator, int denominator) {

	double c = kCentsPerOctave * log2(numerator / (double)denominator);
	if (nextNote >= MAX_DIVISIONS) {
		nextNote = 0;
		c -= kCentsPerOctave;
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

Tuning::Tuning() : divisions(12), nextNote(0), referenceFrequency(440.0) {
	calculateAll();
}

Tuning::Tuning(Tuning& other) : divisions(other.divisions), nextNote(0), referenceFrequency(other.referenceFrequency) {
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

	for (int t = 0; t < NUM_TUNINGS; t++) {
		selectForWrite(t);
		intToString(t, tuning->name, 2);
		for (int i = 0; i < MAX_DIVISIONS; i++) {
			tuning->setOffset(i, 0);
		}
	}

	select(0);
	tuning->setName("TWELVE TONE EDO");

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
