#pragma once

#include "model/scale/note_set.h"
#include "model/scale/scale_change.h"
#include "util/const_functions.h"

#include <cstdint>

#if IN_UNIT_TESTS
#include <iostream>
#endif

class ScaleMapper {
public:
	/** With notes being currently in use, compute ScaleChange to go from sourceScale to targetScale.
	 *
	 * In production code freezes with error if the scale change cannot be computed, for testing
	 * purposes returns false.
	 *
	 * The computed ScaleChanges are such that as long as no new notes are added, all transitions
	 * are reversible.
	 */
	bool computeChangeFrom(NoteSet notes, NoteSet sourceScale, NoteSet targetScale, ScaleChange& changes);

private:
	/** Stores the transition notes from the last computed scale change.
	 */
	NoteSet lastTransitionNotes;
	/** Indexes 0-11 store the transition scales for the corresponding scale size - 1.
	 *
	 * Note: we include 12 tone scales both for simplicity, and in order to support 12-tone scales with
	 * bent notes.
	 */
	NoteSet transitionScaleStore[kMaxScaleSize];
	NoteSet initialTransitionScale(NoteSet sourceScale);
	void computeInitialChanges(NoteSet sourceScale, NoteSet initialScale, ScaleChange& changes);
	void flushTransitionScaleStore(NoteSet initialScale);
	void computeFinalChanges(NoteSet initialScale, NoteSet transitionScale, NoteSet targetScale, ScaleChange& changes);
	NoteSet nextTransitionScale(NoteSet notes, NoteSet transitionScale, NoteSet targetScale);
};
