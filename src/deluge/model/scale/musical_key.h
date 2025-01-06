#pragma once

#include "model/scale/note_set.h"
#include <cstdint>

/** MusicalKey combines a root note and a set of notes relative to the root.
 *
 * modeNotes always contains the root (0).
 */
class MusicalKey {
public:
	MusicalKey();
	/** Returns semitone offset from root below the noteCode. */
	uint8_t intervalOf(int32_t noteCode) const;
	/** Returns degree of noteCode in the scale of they key,
	 * -1 if the noteCode is note NOT in key.
	 */
	int8_t degreeOf(int32_t nodeCode) const;
	// TODO: make these private later, and maybe rename modeNotes
	NoteSet modeNotes;
	int16_t rootNote;
};
