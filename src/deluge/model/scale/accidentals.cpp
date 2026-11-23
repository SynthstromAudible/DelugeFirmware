#include "model/scale/accidentals.h"
#include "model/scale/scale_degrees.h"

#include <cstdlib>

namespace deluge::accidentals {

int32_t lineageAnchor(int32_t pitch, AnchorMarker marker, const SpelledKey& key) {
	if (!marker.isDerived()) {
		return pitch + marker.semitonesToAnchor();
	}
	return key.displayAnchor(pitch);
}

AnchorMarker markerFor(int32_t pitch, int32_t anchor, const SpelledKey& newKey) {
	int32_t derivedAnchor = newKey.displayAnchor(pitch);
	if (anchor == derivedAnchor) {
		return AnchorMarker::derived();
	}
	if (anchor == pitch) {
		return AnchorMarker::self();
	}
	return AnchorMarker::offset(anchor - pitch); // Derived if it doesn't fit
}

namespace {

/// Every note moves with its anchor's degree letter
NoteDestination destinationByLetter(int32_t pitch, AnchorMarker marker, const SpelledKey& oldKey,
                                    const LetterMapping& mapping) {
	int32_t anchor = lineageAnchor(pitch, marker, oldKey);
	int32_t anchorInterval = oldKey.key.intervalOf(anchor);

	// The anchor's letter, and the interval its shift is measured from. An anchor which is a note of the source scale
	// (or a kept one, remembering its letter) is measured from itself; any other is an alteration of the major
	// scale's note of the letter it is spelled with, and is measured from that.
	bool remembersLetter = marker.isTagged();
	bool anchorHasOwnLetter = remembersLetter || mapping.sourceLetter[anchorInterval] >= 0;
	int32_t letter = remembersLetter ? marker.letter() : mapping.sourceLetter[anchorInterval];
	int32_t reference = anchorInterval;
	if (letter < 0) {
		letter = scale_degrees::spellLetter(anchorInterval, mapping.preferFlats);
		reference = scale_degrees::majorNote(letter);
	}

	int32_t primary = mapping.targetPrimary[letter];
	int32_t second = mapping.targetSecond[letter];
	if (primary < 0) {
		// No such degree in the new scale: keep the pitch. The anchor remembers its letter so that it finds its
		// degree again, even if it sits on another degree's pitch meanwhile.
		return {pitch, anchor,
		        anchorHasOwnLetter ? AnchorMarker::tagged(letter, anchor - pitch) : AnchorMarker::derived()};
	}

	bool isSourceScaleNote = anchorHasOwnLetter && !remembersLetter;
	int32_t target = primary;
	int32_t anchorBase = anchor;
	if (second >= 0) {
		// Two notes on this letter. A note of the source scale takes the one whose offset from the target's primary
		// is nearest its own offset from the source's primary (so the 5th goes to the 5th, and a b5 which was a
		// second note on the letter goes to the target's second note), the primary if equally near. Others, and
		// notes remembering a letter, take the one nearest their pitch.
		if (isSourceScaleNote) {
			int32_t offsetFromPrimary = anchorInterval - mapping.sourcePrimary[letter];
			target = std::abs(second - primary - offsetFromPrimary) < std::abs(offsetFromPrimary) ? second : primary;
		}
		else {
			target = std::abs(second - anchorInterval) < std::abs(primary - anchorInterval) ? second : primary;
		}
	}
	else if (isSourceScaleNote) {
		// One note: a note which was a second one on its letter keeps its offset from the primary, so it moves
		// by the primary's shift and hangs off where the primary goes
		reference = mapping.sourcePrimary[letter];
		anchorBase = anchor + (reference - anchorInterval);
	}

	int32_t shift = target - reference;
	int32_t newPitch = pitch + shift;
	int32_t newAnchor = anchorBase + shift;
	if (second >= 0 && target == primary && pitch != anchor && newPitch - newAnchor == second - primary) {
		// Hanging off the primary from exactly where the second note is: it is the second note itself
		newAnchor = newPitch;
	}
	return {newPitch, newAnchor};
}

/// Nothing moves; only the scale around the notes changes
NoteDestination destinationKeepingPitch(int32_t pitch, AnchorMarker marker, const SpelledKey& oldKey,
                                        NoteSet targetScale) {
	// A marked note keeps its marker, as the anchor it names is the one it hangs off.
	if (!marker.isDerived()) {
		return {pitch, lineageAnchor(pitch, marker, oldKey)};
	}
	// An unmarked accidental which is still one in the new key keeps hanging off its anchor if the new key has that
	// too. One that is in the new key now is just a note of it, and one whose anchor isn't in it had that anchor
	// derived for the old key only: both are their own anchor, to be derived again. Notes that were in key hang off
	// themselves, so those which land outside the new key come back to it unmarked.
	int32_t derivedAnchor = oldKey.displayAnchor(pitch);
	bool anchorStays = derivedAnchor != pitch && !targetScale.has(oldKey.key.intervalOf(pitch))
	                   && targetScale.has(oldKey.key.intervalOf(derivedAnchor));
	return {pitch, anchorStays ? derivedAnchor : pitch};
}

/// Each degree of the old key shifts by change[degree] semitones
NoteDestination destinationByDegree(int32_t pitch, AnchorMarker marker, const SpelledKey& oldKey,
                                    const ScaleChange& change, AccidentalHandling handling) {
	const MusicalKey& key = oldKey.key;
	int32_t anchor = lineageAnchor(pitch, marker, oldKey);

	bool isOwnInKeyAnchor = anchor == pitch && key.isInKey(pitch);
	if (!isOwnInKeyAnchor) {
		if (handling == AccidentalHandling::KEEP_PITCH) {
			// Keeps its pitch. The anchor is remembered even if it is no longer in the key (see the F# example in
			// accidentals.h)
			return {pitch, anchor};
		}
		if (!marker.isDerived() && !key.isInKey(anchor)) {
			// Stale marker (its anchor is no longer in the key): ignore it and derive from the pitch
			anchor = oldKey.displayAnchor(pitch);
		}
	}
	if (!key.isInKey(anchor)) {
		return {pitch, pitch}; // No anchor to follow
	}

	int32_t newAnchor = anchor + change[key.degreeOf(anchor)];
	return {newAnchor + (pitch - anchor), newAnchor};
}

} // namespace

NoteDestination destination(int32_t pitch, AnchorMarker marker, const SpelledKey& oldKey, const ScaleChange& change,
                            AccidentalHandling handling) {
	if (change.letterMapping) {
		return destinationByLetter(pitch, marker, oldKey, *change.letterMapping);
	}
	if (change.isIdentity()) {
		return destinationKeepingPitch(pitch, marker, oldKey, change.target);
	}
	return destinationByDegree(pitch, marker, oldKey, change, handling);
}

} // namespace deluge::accidentals
