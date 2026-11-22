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

	/** True if no degree moves. With source != target this is a pitch-preserving change: every note keeps its pitch
	 * and only the scale around them changes.
	 */
	bool isIdentity() const {
		if (byLetter) {
			return false;
		}
		for (int8_t offset : degreeOffset) {
			if (offset != 0) {
				return false;
			}
		}
		return true;
	}

	/** If set, notes are mapped by degree letter (see scale_degrees.h) instead of by scale degree:
	 * sourceLetter[interval] is the letter of each note of the source scale (-1 if not in it), targetNote[letter] the
	 * interval of the target scale's note with that letter (-1 if it has none), and preferFlats how anchors outside the
	 * source scale are spelled. The degree offsets are unused.
	 */
	bool byLetter = false;
	bool preferFlats = false;
	int8_t sourceLetter[12] = {0};
	int8_t targetNote[7] = {0};

private:
	int8_t degreeOffset[kMaxScaleSize] = {0};
};
