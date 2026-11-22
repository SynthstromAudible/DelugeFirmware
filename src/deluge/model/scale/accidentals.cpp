#include "model/scale/accidentals.h"
#include "model/scale/scale_degrees.h"

#include <cstdlib>

namespace deluge::accidentals {

int32_t lineageAnchor(int32_t y, int8_t m, const MusicalKey& k) {
	if (m == kSelfAnchored) {
		return y;
	}
	if (isTagged(m)) {
		return y + taggedOffset(m);
	}
	if (m != 0) {
		return y + m;
	}
	if (k.isInKey(y)) {
		return y;
	}
	return k.accidentalAnchor(y, k.prefersFlats());
}

NoteDestination destination(int32_t y, int8_t m, const MusicalKey& oldKey, const ScaleChange& c, bool followAnchor) {
	if (c.byLetter) {
		// The anchor's letter L, and the pitch the shift is measured from: the anchor itself if it is a note of the
		// source scale or a kept note (alteration 0), else the major scale's note of L (the anchor is an alteration
		// of it).
		int32_t a = lineageAnchor(y, m, oldKey);
		int interval = oldKey.intervalOf(a);
		bool native = isTagged(m) || c.sourceLetter[interval] >= 0;
		int letter = isTagged(m) ? taggedLetter(m) : c.sourceLetter[interval];
		int reference = interval;
		if (letter < 0) {
			letter = deluge::scale_degrees::spellLetter(interval, c.preferFlats);
			reference = deluge::scale_degrees::majorNote(letter);
		}
		if (c.targetNote[letter] < 0) {
			// No such degree in the new scale: keep the pitch. The anchor remembers its letter so that it finds its
			// degree again, even if it sits on another degree's pitch meanwhile.
			return {y, a, native ? tagged(letter, a - y) : (int8_t)0};
		}
		int primary = c.targetNote[letter];
		int second = c.targetSecond[letter];
		int target = primary;
		int32_t anchorBase = a;
		if (second >= 0) {
			// Two notes on this letter. A note of the source scale takes the one whose offset from the target's primary
			// is nearest its own offset from the source's primary (so the 5th goes to the 5th, and a b5 which was a
			// second note on the letter goes to the target's second note), the primary if equally near. Others, and
			// notes remembering a letter, take the one nearest their pitch.
			if (native && !isTagged(m)) {
				int offset = interval - c.sourcePrimary[letter];
				target = std::abs(second - primary - offset) < std::abs(offset) ? second : primary;
			}
			else {
				target = std::abs(second - interval) < std::abs(primary - interval) ? second : primary;
			}
		}
		else if (native && !isTagged(m)) {
			// One note: a note which was a second one on its letter keeps its offset from the primary, so it moves
			// by the primary's shift and hangs off where the primary goes
			reference = c.sourcePrimary[letter];
			anchorBase = a + (reference - interval);
		}
		int32_t shift = target - reference;
		int32_t newPitch = y + shift;
		int32_t newAnchor = anchorBase + shift;
		if (second >= 0 && target == primary && y != a && newPitch - newAnchor == second - primary) {
			// Hanging off the primary from exactly where the second note is: it is the second note itself
			newAnchor = newPitch;
		}
		return {newPitch, newAnchor};
	}
	if (c.isIdentity()) {
		// Pitch-preserving: nothing moves. A marked note keeps its marker, as the anchor it names is the one it hangs
		// off. An unmarked accidental which is still one in the new key keeps hanging off its anchor if the new key has
		// that too. One that is in the new key now is just a note of it, and one whose anchor isn't in it had that
		// anchor derived for the old key only: both are unmarked, and the anchor derived again. Notes that were in key
		// hang off themselves, so those which land outside the new key come back to it unmarked.
		if (m != 0) {
			return {y, lineageAnchor(y, m, oldKey)};
		}
		int32_t derivedAnchor = lineageAnchor(y, 0, oldKey);
		bool anchorStays =
		    derivedAnchor != y && !c.target.has(oldKey.intervalOf(y)) && c.target.has(oldKey.intervalOf(derivedAnchor));
		return {y, anchorStays ? derivedAnchor : y};
	}
	int32_t a = lineageAnchor(y, m, oldKey);
	if (a == y && oldKey.degreeOf(y) >= 0) {
		int32_t p = y + c[oldKey.degreeOf(y)];
		return {p, p};
	}

	if (!followAnchor) {
		// The anchor is remembered even if it is no longer in the key (see the F# example in accidentals.h)
		return {y, a};
	}

	if (oldKey.degreeOf(a) < 0 && m != 0) {
		// Stale marker (its anchor is no longer in the key): ignore it and derive from the pitch
		return destination(y, 0, oldKey, c, followAnchor);
	}
	if (oldKey.degreeOf(a) < 0) {
		return {y, y}; // No anchor to follow
	}

	int32_t newAnchor = a + c[oldKey.degreeOf(a)];
	return {newAnchor + (y - a), newAnchor};
}

int8_t markerFor(int32_t p, int32_t a, const MusicalKey& newKey) {
	int32_t derived = newKey.isInKey(p) ? p : newKey.accidentalAnchor(p, newKey.prefersFlats());
	int32_t offset = a - p;
	if (offset == 0) {
		return a == derived ? 0 : kSelfAnchored;
	}
	if (a == derived || offset < kLowestPlainOffset || offset > 127) {
		return 0;
	}
	return (int8_t)offset;
}

} // namespace deluge::accidentals
