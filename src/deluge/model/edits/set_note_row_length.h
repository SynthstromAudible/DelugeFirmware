#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief Set a note-row's independent length (the NOTEROW_LENGTH_EDIT new-action case).
///
/// apply() opens a NOTEROW_LENGTH_EDIT Action and adds a ConsequenceNoteRowLength whose performChange both applies
/// the length change and records its own undo. Returns Error::INSUFFICIENT_RAM on allocation failure.
class SetNoteRowLength final : public Edit {
public:
	SetNoteRowLength(std::optional<project::NoteRowAddress> address, int32_t newLength, int32_t oldPos,
	                 bool hadIndependentPlayPosBefore, ModelStackWithNoteRow* engineStack)
	    : address_(address), newLength_(newLength), oldPos_(oldPos),
	      hadIndependentPlayPosBefore_(hadIndependentPlayPosBefore), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t newLength_;
	int32_t oldPos_;
	bool hadIndependentPlayPosBefore_;
	ModelStackWithNoteRow* engineStack_; ///< Borrowed for the duration of apply().
};

} // namespace deluge::edits
