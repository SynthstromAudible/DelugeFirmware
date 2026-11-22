#pragma once

#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/scale_change.h"

#include <array>
#include <cstdint>

/** Diatonic degree letters: which 1st..7th (measured against the major scale 0 2 4 5 7 9 11) each note of a scale
 * is, so a scale change can map degree to degree (pentatonic minor is 1 b3 4 5 b7) rather than by index.
 */
namespace deluge::scale_degrees {

/** Fills letters[interval] with the letter 0..6 of each note of a scale of at most 7 notes, -1 for intervals not in
 * it. 7-note scales: letter = index. Fewer notes: the order-preserving, injective assignment with the root on letter
 * 0 which minimises the total alteration from the major scale (at most 2 each); ties go to flats (a sharp costs a bit
 * more than a flat). Returns false, with letters all -1, if the scale has more than 7 notes or no such assignment
 * exists. The root is always counted as present.
 */
bool degreeLetters(NoteSet scale, std::array<int8_t, 12>& letters);

/** Letter 0..6 an interval outside the scale is spelled with against the major scale: the next letter below it (a
 * sharp) or, if flats, the next at or above it (a flat).
 */
int spellLetter(int interval, bool flats);

/** Splits the notes sharing letters into primary and secondary: for each letter 0..6, the interval of its primary note
 * (-1 if none) and of its second note (-1 if only one). The primary is the one nearer the major scale's note, the
 * lower one if equally near.
 */
void letterNotes(const std::array<int8_t, 12>& letters, int8_t primary[7], int8_t secondary[7]);

/** Interval of the major scale's note with this letter. */
int majorNote(int letter);

/** Builds a ScaleChange from key.modeNotes to target which maps every note by its degree letter (see
 * ScaleChange::byLetter). Returns false, leaving change alone, if either scale has no letters.
 * key decides how anchors which aren't in the source scale are spelled (flats or sharps).
 */
bool letterChange(const MusicalKey& key, NoteSet target, ScaleChange& change);

} // namespace deluge::scale_degrees
