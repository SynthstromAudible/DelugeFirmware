#pragma once

#include "model/scale/note_set.h"
#include <array>
#include <cstdint>
#include <optional>

/** How a ScaleChange maps notes when it goes by diatonic degree letter (see scale_degrees.h) rather than by scale
 * degree index. Intervals are semitones above the root; -1 means "none" throughout.
 */
struct LetterMapping {
	/// How anchors outside the source scale are spelled
	bool preferFlats = false;
	/// Letter 0..6 of each interval of the source scale
	std::array<int8_t, 12> sourceLetter{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	/// For each letter, the interval of the source scale's (primary) note with it
	std::array<int8_t, 7> sourcePrimary{-1, -1, -1, -1, -1, -1, -1};
	/// For each letter, the interval of the target scale's (primary) note with it
	std::array<int8_t, 7> targetPrimary{-1, -1, -1, -1, -1, -1, -1};
	/// For each letter, the interval of the target scale's second note with it, if two share the letter
	std::array<int8_t, 7> targetSecond{-1, -1, -1, -1, -1, -1, -1};
};

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
		if (letterMapping) {
			return false;
		}
		for (int8_t offset : degreeOffset) {
			if (offset != 0) {
				return false;
			}
		}
		return true;
	}

	/** If set, notes are mapped by degree letter instead of by scale degree, and the degree offsets are unused. */
	std::optional<LetterMapping> letterMapping;

private:
	int8_t degreeOffset[kMaxScaleSize] = {0};
};
