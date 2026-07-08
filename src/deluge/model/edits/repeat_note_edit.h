#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Change the number of note-repeats within one square (the reification of InstrumentClipView's NOTE_REPEAT_EDIT
/// gesture). apply reproduces the call site's two paths: a "secret undo" when the last open Action was the
/// equal-and-opposite repeat edit, else a fresh Action + editNoteRepeatAcrossAllScreens (which records its own
/// undo into the Action). The view keeps the clamp/display/render; newNumNotes arrives already clamped.
class RepeatNoteEdit final : public Edit {
public:
	RepeatNoteEdit(std::optional<project::NoteRowAddress> address, int32_t offset, int32_t squareStart,
	               int32_t squareWidth, int32_t newNumNotes, uint32_t wrapEditLevel, ModelStackWithNoteRow* engineStack)
	    : address_(address), offset_(offset), squareStart_(squareStart), squareWidth_(squareWidth),
	      newNumNotes_(newNumNotes), wrapEditLevel_(wrapEditLevel), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t offset_;
	int32_t squareStart_;
	int32_t squareWidth_;
	int32_t newNumNotes_;
	uint32_t wrapEditLevel_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
