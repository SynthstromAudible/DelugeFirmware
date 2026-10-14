#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief Delete the note at `pos` in a melodic note-row (a pad-tap note removal).
///
/// apply() opens a NOTE_EDIT Action and calls NoteRow::deleteNoteByPos, which records the note-array undo into it.
class RemoveNote final : public Edit {
public:
	/// @param address     Model address of the note-row, if it has one.
	/// @param pos         Tick position of the note to remove.
	/// @param engineStack Stack for the note-row (borrowed for the duration of apply()).
	RemoveNote(std::optional<project::NoteRowAddress> address, int32_t pos, ModelStackWithNoteRow* engineStack)
	    : address_(address), pos_(pos), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t pos_;
	ModelStackWithNoteRow* engineStack_; ///< Borrowed for the duration of apply().
};

} // namespace deluge::edits
