#include "model/scale/musical_key.h"
#include "util/const_functions.h"

#include <cstdint>

MusicalKey::MusicalKey() {
	modeNotes.add(0);
	rootNote = 0;
}

uint8_t MusicalKey::intervalOf(int32_t noteCode) const {
	return mod(noteCode - rootNote, 12);
}

int8_t MusicalKey::degreeOf(int32_t noteCode) const {
	return modeNotes.degreeOf(intervalOf(noteCode));
}

bool MusicalKey::prefersFlats() const {
	// Find the relative major tonic: the offset t from the root such that the key's notes are the major scale
	// starting t semitones above the root.
	NoteSet major{0, 2, 4, 5, 7, 9, 11};
	int32_t t = modeNotes.majorness() >= 0 ? 0 : 3;
	for (int32_t candidate = 0; candidate < 12; candidate++) {
		if (major.modulateByOffset(candidate) == modeNotes) {
			t = candidate;
			break;
		}
	}
	int32_t tonic = mod(rootNote + t, 12);
	int32_t fifths = (tonic * 7) % 12;
	return fifths > 6; // C (no accidentals) and F#/Gb (6 either way) count as sharps
}

int32_t MusicalKey::accidentalAnchor(int32_t noteCode, bool flats) const {
	int32_t step = flats ? 1 : -1;
	int32_t note = noteCode;
	for (int32_t i = 0; i < 12 && !isInKey(note); i++) {
		note += step;
	}
	return note;
}
