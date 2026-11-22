#pragma once

#include "model/scale/musical_key.h"
#include "model/scale/scale_change.h"

#include <cstdint>

/** Pure helpers for "accidental lineage": notes outside the key (accidentals) hang off an in-key anchor pitch, and
 * follow it through scale changes. A Note stores anchorOffset (anchor - pitch) only when that can't be derived from
 * the pitch and the key (e.g. after rows were merged by a scale change); 0 means "derive".
 */
namespace deluge::accidentals {

/** Reserved markers (not offsets: never add them to a pitch). Both mean the anchor is the pitch of the note itself, or
 * an anchor which kept its pitch when its degree was absent from the new scale and so is no longer where the degree
 * is. kSelfAnchored: anchor = the note's own pitch, even though the key would derive another. tagged(letter, offset):
 * anchor = y + offset, remembering the degree letter 0..6 it was a note of (offset 0 is a note that is its own
 * anchor), so it finds the degree again even when its pitch is a native note of another degree meanwhile. They
 * occupy [INT8_MIN, kLowestPlainOffset); plain offsets are only used from kLowestPlainOffset up.
 */
constexpr int8_t kSelfAnchored = INT8_MIN;
constexpr int kTagMaxOffset = 4;
constexpr int kTagSpan = 2 * kTagMaxOffset + 1;
constexpr int8_t kTagBase = INT8_MIN + 1;
constexpr int8_t kLowestPlainOffset = kTagBase + 7 * kTagSpan;
/** The marker for an anchor at y + offset which is the note of degree letter `letter`, or 0 if offset doesn't fit. */
constexpr int8_t tagged(int letter, int offset) {
	return (offset < -kTagMaxOffset || offset > kTagMaxOffset)
	           ? (int8_t)0
	           : (int8_t)(kTagBase + letter * kTagSpan + offset + kTagMaxOffset);
}
constexpr int8_t selfAnchored(int letter) {
	return tagged(letter, 0);
}
constexpr bool isTagged(int8_t m) {
	return m >= kTagBase && m < kLowestPlainOffset;
}
constexpr int taggedLetter(int8_t m) {
	return (m - kTagBase) / kTagSpan;
}
constexpr int taggedOffset(int8_t m) {
	return (m - kTagBase) % kTagSpan - kTagMaxOffset;
}
/** True for markers which make the note its own anchor. */
constexpr bool isSelfAnchored(int8_t m) {
	return m == kSelfAnchored || (isTagged(m) && taggedOffset(m) == 0);
}

/** Lineage anchor of a note at pitch y with stored offset m under key k:
 * kSelfAnchored -> y; tagged -> y + its offset; m != 0 -> y + m; y in key -> y; otherwise the display anchor (below in
 * sharp keys, above in flat keys).
 */
int32_t lineageAnchor(int32_t y, int8_t m, const MusicalKey& k);

struct NoteDestination {
	int32_t pitch;
	int32_t anchor;
	/// Nonzero if the note must be marked with this (selfAnchored(letter)) rather than with markerFor()
	int8_t marker = 0;
};

/** Where a note goes under ScaleChange c (degrees in oldKey). If c.byLetter, every note moves by its anchor's degree
 * letter (followAnchor is ignored: the set/scale-select path); a note whose letter the target lacks keeps its pitch. A
 * note with an in-key anchor equal to its pitch moves by its own degree's shift. Accidental-lineage notes (anchor !=
 * pitch) either follow their anchor (followAnchor: scale cycle/set, rotate) or keep their pitch (sharpen/flatten one
 * degree). anchor is the lineage anchor after the change, before it is turned into a marker with markerFor().
 * Keep-pitch example: C major with an F# accidental, sharpen F: the F row lands on F#; the accidental stays on F# with
 * anchor F, so it is marked -1 and splits back out when F# is flattened back to F.
 */
NoteDestination destination(int32_t y, int8_t m, const MusicalKey& oldKey, const ScaleChange& c, bool followAnchor);

/** Marker to store for a note landing at pitch p with anchor a under newKey (0 if derivable, kSelfAnchored if a == p
 * but the key would derive another). */
int8_t markerFor(int32_t p, int32_t a, const MusicalKey& newKey);

} // namespace deluge::accidentals
