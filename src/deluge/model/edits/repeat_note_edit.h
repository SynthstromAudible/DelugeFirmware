#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief Change the number of note-repeats within one square (the NOTE_REPEAT_EDIT gesture).
///
/// apply() takes one of two paths: a "secret undo" when the last open Action was the equal-and-opposite repeat
/// edit, otherwise a fresh Action plus editNoteRepeatAcrossAllScreens (which records its own undo into the Action).
/// The view owns the clamp, display and render; `newNumNotes` must arrive already clamped.
class RepeatNoteEdit final : public Edit {
public:
	/// @param address     Model address of the note-row, if it has one.
	/// @param offset      Signed change in repeat count; the secret-undo path matches on its negation.
	/// @param squareStart Tick position where the square starts.
	/// @param squareWidth Width of the square in ticks.
	/// @param newNumNotes Resulting number of repeats, already clamped.
	/// @param wrapEditLevel Screen width in ticks: the edit applies at the same offset in every screen of this width.
	/// @param engineStack Stack for the note-row (borrowed for the duration of apply()).
	RepeatNoteEdit(std::optional<project::NoteRowAddress> address, int32_t offset, int32_t squareStart,
	               int32_t squareWidth, int32_t newNumNotes, uint32_t wrapEditLevel, ModelStackWithNoteRow* engineStack)
	    : address_(address), offset_(offset), squareStart_(squareStart), squareWidth_(squareWidth),
	      newNumNotes_(newNumNotes), wrapEditLevel_(wrapEditLevel), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t offset_;
	int32_t squareStart_;
	int32_t squareWidth_;
	int32_t newNumNotes_;
	uint32_t wrapEditLevel_;
	ModelStackWithNoteRow* engineStack_; ///< Borrowed for the duration of apply().
};

} // namespace deluge::edits
