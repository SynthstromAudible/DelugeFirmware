#include "model/scale/accidentals.h"

namespace deluge::accidentals {

int32_t lineageAnchor(int32_t y, int8_t m, const MusicalKey& k) {
	if (m != 0) {
		return y + m;
	}
	if (k.isInKey(y)) {
		return y;
	}
	return k.accidentalAnchor(y, k.prefersFlats());
}

NoteDestination destination(int32_t y, int8_t m, const MusicalKey& oldKey, const ScaleChange& c, bool followAnchor) {
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
	if (a == derived || offset == 0 || offset < -128 || offset > 127) {
		return 0;
	}
	return (int8_t)offset;
}

} // namespace deluge::accidentals
