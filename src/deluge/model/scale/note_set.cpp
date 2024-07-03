#include "model/scale/note_set.h"

void NoteSet::addUntrusted(uint8_t note) {
	// Constrain added note to be strictly increasing, and < 12.
	//
	// Used when reading mode notes from song files, and other
	// untrusted sources.
	if (count() > 0 && note <= highest()) {
		note = highest() + 1;
	}
	if (note > 11) {
		note = 11;
	}
	add(note);
}

uint8_t NoteSet::operator[](uint8_t index) const {
	// Shift out the root note bit, ie. scale degree zero. We don't
	// actually check it it's set, because counting scale degrees
	// doesn't make sense if the root is not set.
	uint16_t wip = bits >> 1;
	uint8_t note = 0;
	while (index--) {
		if (!wip) {
			// The desired degree does not exist.
			return 0;
		}
		// For each subsequent scale degree, find the number
		// of semitones, increment the note counter
		// and shift out the bit.
		uint8_t step = std::countr_zero(wip) + 1;
		wip >>= step;
		note += step;
	}
	// Return the note
	return note;
}

void NoteSet::fromScaleNotes(const uint8_t scaleNotes[7]) {
	clear();
	for (int32_t n = 0; n < 7; n++) {
		int32_t newNote = scaleNotes[n];
		// Non-diatonic scales have trailing zero notes
		// but adding zero to the set again doesn't hurt.
		add(newNote);
	}
}

void NoteSet::applyChanges(int8_t changes[12]) {
	NoteSet newSet;
	uint8_t n = 1;
	for (int note = 1; note < 12; note++) {
		if (has(note)) {
			// n'th degree has semitone t, compute
			// the transpose and save to new noteset
			newSet.add(note + changes[n++] - changes[0]);
		}
	}
	newSet.add(0);
	bits = newSet.bits;
}
