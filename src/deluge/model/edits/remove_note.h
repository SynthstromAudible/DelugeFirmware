#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Delete the note at `pos` in a melodic note-row — reifies the pad-tap note-remove. apply funnels
/// getNewAction(NOTE_EDIT)+NoteRow::deleteNoteByPos (which records the note-array undo into the Action).
class RemoveNote final : public Edit {
public:
	RemoveNote(std::optional<project::NoteRowAddress> address, int32_t pos, ModelStackWithNoteRow* engineStack)
	    : address_(address), pos_(pos), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t pos_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
