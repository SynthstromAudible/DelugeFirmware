#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Set the number of Euclidean events on a note-row (the reification of InstrumentClipView's
/// EUCLIDEAN_NUM_EVENTS_EDIT gesture). apply reproduces the call site's two paths exactly: a "partial undo" when the
/// last open Action was the equal-and-opposite euclidean edit, else a fresh Action that rebuilds the note pattern,
/// steals the MPE automation + old note array into undo Consequences, and swaps the new notes in. The view keeps the
/// clamp/display logic; newNumNotes arrives already clamped. Returns the NoteVector-preallocation error so the call
/// site keeps its displayError flow.
class SetEuclideanEvents final : public Edit {
public:
	SetEuclideanEvents(std::optional<project::NoteRowAddress> address, int32_t offset, int32_t newNumNotes,
	                   int32_t numStepsAvailable, int32_t effectiveLength, uint32_t squareWidth,
	                   ModelStackWithNoteRow* engineStack)
	    : address_(address), offset_(offset), newNumNotes_(newNumNotes), numStepsAvailable_(numStepsAvailable),
	      effectiveLength_(effectiveLength), squareWidth_(squareWidth), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t offset_;
	int32_t newNumNotes_;
	int32_t numStepsAvailable_;
	int32_t effectiveLength_;
	uint32_t squareWidth_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
