#pragma once

#include "music-theory/note_set.h"
#include "music-theory/scale_change.h"

#include <cstdint>

namespace deluge::music {

class ScaleMapper {
public:
	/// @brief Compute the ScaleChange that takes the notes in use from @p sourceScale to @p targetScale.
	///
	/// In production code freezes with error if the scale change cannot be computed; for testing
	/// purposes returns false.
	///
	/// The computed ScaleChanges are such that as long as no new notes are added, all transitions
	/// are reversible.
	///
	/// @param notes       The notes currently in use.
	/// @param sourceScale The scale being left.
	/// @param targetScale The scale being entered.
	/// @param changes     Receives the computed change.
	/// @return True if the change was computed; false if it could not be (test builds only).
	bool computeChangeFrom(NoteSet notes, NoteSet sourceScale, NoteSet targetScale, ScaleChange& changes);

private:
	/// Stores the transition notes from the last computed scale change.
	NoteSet lastTransitionNotes;
	/// Indexes 0-11 store the transition scales for the corresponding scale size - 1.
	///
	/// Note: we include 12 tone scales both for simplicity, and in order to support 12-tone scales with
	/// bent notes.
	NoteSet transitionScaleStore[kMaxScaleSize]{};
	NoteSet initialTransitionScale(NoteSet sourceScale);
	void computeInitialChanges(NoteSet sourceScale, NoteSet initialScale, ScaleChange& changes);
	void flushTransitionScaleStore(NoteSet initialScale);
	void computeFinalChanges(NoteSet initialScale, NoteSet transitionScale, NoteSet targetScale, ScaleChange& changes);
	NoteSet nextTransitionScale(NoteSet notes, NoteSet transitionScale, NoteSet targetScale);
};

} // namespace deluge::music
