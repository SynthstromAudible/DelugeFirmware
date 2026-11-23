#pragma once

#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/scale_change.h"

#include <array>
#include <cstdint>
#include <optional>

/** Diatonic degree letters: which 1st..7th (measured against the major scale 0 2 4 5 7 9 11) each note of a scale
 * is, so a scale change can map degree to degree (pentatonic minor is 1 b3 4 5 b7) rather than by index.
 */
namespace deluge::scale_degrees {

/// Letter 0..6 of each interval of a scale, -1 for intervals not in it
using DegreeLetters = std::array<int8_t, 12>;

/** The letters of the notes of a scale of at most 7 notes. 7-note scales: letter = index. Fewer notes: the
 * order-preserving assignment with the root alone on letter 0 and at most two notes per letter which minimises the
 * total alteration from the major scale (at most 2 each); sharing a letter costs extra, and ties go to flats (a sharp
 * costs a bit more than a flat). Returns nullopt if the scale has more than 7 notes or no such assignment exists.
 * The root is always counted as present.
 */
std::optional<DegreeLetters> degreeLetters(NoteSet scale);

/** Letter 0..6 an interval outside the scale is spelled with against the major scale: the next letter below it (a
 * sharp) or, if flats, the next at or above it (a flat).
 */
int spellLetter(int interval, bool flats);

/// For each letter 0..6, the intervals of the scale's notes with it. -1 means "none".
struct LetterNotes {
	std::array<int8_t, 7> primary;
	std::array<int8_t, 7> secondary; ///< Only where two notes share the letter
};

/** Splits the notes sharing letters into primary and secondary. The primary is the one nearer the major scale's note,
 * the lower one if equally near.
 */
LetterNotes letterNotes(const DegreeLetters& letters);

/** Interval of the major scale's note with this letter. */
int majorNote(int letter);

/** Builds a ScaleChange from key.modeNotes to target which maps every note by its degree letter (see LetterMapping).
 * Returns nullopt if either scale has no letters. key also decides how anchors which aren't in the source scale are
 * spelled (flats or sharps).
 */
std::optional<ScaleChange> letterChange(const MusicalKey& key, NoteSet target);

} // namespace deluge::scale_degrees
