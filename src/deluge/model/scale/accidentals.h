#pragma once

#include "model/scale/musical_key.h"
#include "model/scale/scale_change.h"

#include <cstdint>

/** Pure helpers for "accidental lineage": notes outside the key (accidentals) hang off an in-key anchor pitch, and
 * follow it through scale changes. A Note stores anchorOffset (anchor - pitch) only when that can't be derived from
 * the pitch and the key (e.g. after rows were merged by a scale change); 0 means "derive".
 */
namespace deluge::accidentals {

/** Lineage anchor of a note at pitch y with stored offset m under key k:
 * m != 0 -> y + m; y in key -> y; otherwise the display anchor (below in sharp keys, above in flat keys).
 */
int32_t lineageAnchor(int32_t y, int8_t m, const MusicalKey& k);

struct NoteDestination {
	int32_t pitch;
	int32_t anchor;
};

/** Where a note goes under ScaleChange c (degrees in oldKey). A note with an in-key anchor equal to its pitch moves
 * by its own degree's shift. Accidental-lineage notes (anchor != pitch) either follow their anchor (followAnchor:
 * scale cycle/set, rotate) or keep their pitch (sharpen/flatten one degree). anchor is the lineage anchor after the
 * change, before it is turned into a marker with markerFor(). Keep-pitch example: C major with an F# accidental,
 * sharpen F: the F row lands on F#; the accidental stays on F# with anchor F, so it is marked -1 and splits back out
 * when F# is flattened back to F.
 */
NoteDestination destination(int32_t y, int8_t m, const MusicalKey& oldKey, const ScaleChange& c, bool followAnchor);

/** Marker to store for a note landing at pitch p with anchor a under newKey (0 if derivable). */
int8_t markerFor(int32_t p, int32_t a, const MusicalKey& newKey);

} // namespace deluge::accidentals
