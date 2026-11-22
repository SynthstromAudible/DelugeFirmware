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
	/** True if the key's scale is spelled with flats (relative major tonic is F, Bb, Eb, Ab, Db, ...), false for
	 * sharps (G, D, A, E, B). Non-diatonic scales use the relative major of the root (major-ish) or of the root + 3
	 * semitones (minor-ish). Keys with no preference (relative major C, or F#/Gb) use sharps. Decides both note names
	 * and which row an accidental is drawn on.
	 */
	bool prefersFlats() const;
	/** True if noteCode (any octave) is a note of the key's scale. */
	bool isInKey(int32_t noteCode) const { return modeNotes.has(intervalOf(noteCode)); }
	/** For a note outside the key, returns the in-key note it is displayed on: the nearest in-key note below
	 * (sharps) or above (flats). Returns noteCode itself if it is in key.
	 */
	int32_t accidentalAnchor(int32_t noteCode, bool flats) const;
	// TODO: make these private later, and maybe rename modeNotes
	NoteSet modeNotes;
	int16_t rootNote;
};
