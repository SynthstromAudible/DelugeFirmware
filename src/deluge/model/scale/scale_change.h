#pragma once

#include "model/scale/note_set.h"

class ScaleChange {
public:
	/** The source scale to which the ScaleChange applies.
	 */
	NoteSet source;
	NoteSet target;
	/** Returns the number of semitones for a scale degree offset.
	 */
	int8_t operator[](uint8_t degree) const { return degreeOffset[degree]; }
	int8_t& operator[](uint8_t degree) { return degreeOffset[degree]; }
	/** Transposes a NoteSet according to the ScaleChange.
	 *
	 * For each scale degree of source, if the note exists in the
	 * notes, applies the semitone offset from the corresponding
	 * scale degree.
	 *
	 * If the NoteSet is not a subset of oldScale, all bets are off.
	 */
	NoteSet applyTo(NoteSet notes) const;

private:
	int8_t degreeOffset[kMaxScaleSize] = {0};
};
