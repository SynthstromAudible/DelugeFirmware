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
