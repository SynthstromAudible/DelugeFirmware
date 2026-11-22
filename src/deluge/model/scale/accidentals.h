#pragma once

#include "model/scale/anchor_marker.h"
#include "model/scale/musical_key.h"
#include "model/scale/scale_change.h"

#include <cstdint>

/** Pure helpers for "accidental lineage": notes outside the key (accidentals) hang off an in-key anchor pitch, and
 * follow it through scale changes. A Note stores an AnchorMarker only when its anchor can't be derived from its pitch
 * and the key (e.g. after a scale change put it on an in-key pitch).
 */
namespace deluge::accidentals {

/** A key together with whether it is spelled with flats. Working that out is not free (MusicalKey::prefersFlats()),
 * so build one of these per key rather than per note.
 */
struct SpelledKey {
	explicit SpelledKey(const MusicalKey& key_) : key(key_), flats(key_.prefersFlats()) {}

	/// The in-key note that noteCode is displayed on: itself if in key, else the nearest in-key note below it (sharp
	/// keys) or above it (flat keys)
	[[nodiscard]] int32_t displayAnchor(int32_t noteCode) const { return key.accidentalAnchor(noteCode, flats); }

	MusicalKey key;
	bool flats;
};

/** The anchor of a note at pitch with the given marker: the one the marker names, or for a derived (unmarked) note
 * its display anchor in the key (which is the pitch itself if that is in key).
 */
int32_t lineageAnchor(int32_t pitch, AnchorMarker marker, const SpelledKey& key);

/** Marker to store for a note which a scale change left at pitch with the given anchor, under the key after the
 * change: derived if that key gives the pitch this anchor anyway (or the distance can't be stored), self if the note
 * is its own anchor but the key would derive another, else the offset.
 */
AnchorMarker markerFor(int32_t pitch, int32_t anchor, const SpelledKey& newKey);

/// What a scale change which shifts scale degrees does with accidentals
enum class AccidentalHandling {
	/// They move along with the in-key note they hang off
	FOLLOW_ANCHOR,
	/// They stay at their pitch (sharpening or flattening one degree)
	KEEP_PITCH,
};

/// Where a scale change puts a note
struct NoteDestination {
	int32_t pitch;
	/// The note's lineage anchor after the change
	int32_t anchor;
	/// If not derived, the note must be given this marker (a tagged one, which markerFor() can't work out as it
	/// doesn't know the letter) instead of markerFor()'s
	AnchorMarker forcedMarker = AnchorMarker::derived();

	/// The marker to store on the note. pendingTranspose is a uniform shift in semitones which the caller is still to
	/// apply to every note and to the key's root after the change (in-key transposition does this), and finalKey the
	/// key as it will be once that is done.
	[[nodiscard]] AnchorMarker markerUnder(const SpelledKey& finalKey, int32_t pendingTranspose = 0) const {
		return forcedMarker.isDerived() ? markerFor(pitch + pendingTranspose, anchor + pendingTranspose, finalKey)
		                                : forcedMarker;
	}
};

/** Where a note at pitch with the given marker goes under a scale change away from oldKey. There are three kinds of
 * change:
 * - By letter (change.letterMapping is set; scale selection): every note moves with its anchor's degree letter, and
 *   one whose letter the target scale lacks keeps its pitch. handling is ignored.
 * - Pitch-preserving (change.isIdentity()): no note moves; only the anchors are worked out again.
 * - By degree (rotating the mode, nudging by scale degrees, sharpening or flattening one degree): a note which is its
 *   own in-key anchor moves by its degree's shift. What an accidental does is up to handling.
 *   Keep-pitch example: C major with an F# accidental, sharpen F: the F row lands on F#; the accidental stays on F#
 *   with anchor F, so it is marked offset(-1) and splits back out when F# is flattened back to F.
 */
NoteDestination destination(int32_t pitch, AnchorMarker marker, const SpelledKey& oldKey, const ScaleChange& change,
                            AccidentalHandling handling);

} // namespace deluge::accidentals
