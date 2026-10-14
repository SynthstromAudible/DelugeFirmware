#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief Set the number of Euclidean events on a note-row (the EUCLIDEAN_NUM_EVENTS_EDIT gesture).
///
/// apply() takes one of two paths: a "partial undo" when the last open Action was the equal-and-opposite euclidean
/// edit, else a fresh Action that rebuilds the note pattern, steals the MPE automation + old note array into undo
/// Consequences, and swaps the new notes in. The caller owns clamping and display; newNumNotes must arrive already
/// clamped. apply() returns the NoteVector-preallocation error so the caller can show it.
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
	ModelStackWithNoteRow* engineStack_; ///< Borrowed for the duration of apply().
};

} // namespace deluge::edits
