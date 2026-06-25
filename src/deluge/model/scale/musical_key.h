#pragma once

#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h" // Scale (return type of getCurrentScale)
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

	/** Maps a note code to its vertical display position within this key (identity outside key mode). */
	int32_t getYVisualFromYNote(int32_t yNote, bool inKeyMode) const;
	/** Maps a vertical display position back to a note code within this key (identity outside key mode). */
	int32_t getYNoteFromYVisual(int32_t yVisual, bool inKeyMode) const;
	/** Steps a note up/down by `increment` scale degrees, optionally wrapping within the octave. */
	int32_t incrementYNoteInKey(int32_t yNote, int32_t increment, bool inOctave) const;
	/** Whether the mode note at `yVisualWithinOctave` may be nudged by `newOffset` without colliding. */
	bool mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset) const;
	/** The preset Scale implied by this key's mode notes (NUM_PRESET_SCALES if it is a user scale). */
	Scale getCurrentScale() const;

	// TODO: make these private later, and maybe rename modeNotes
	NoteSet modeNotes;
	int16_t rootNote{};
};
