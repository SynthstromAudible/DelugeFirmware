#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Clear a single note-row's contents (the note-row-scoped variant of the CLIP_CLEAR gesture — the automation-view
/// note-editor "clear" acts on one row, not the whole clip). apply() opens the Action and calls noteRow->clear(),
/// which self-records the undo. The view keeps the clear flags, the note-row-exists guard, and the popup.
class ClearNoteRow final : public Edit {
public:
	ClearNoteRow(std::optional<project::NoteRowAddress> address, bool clearAutomation, bool clearSequenceAndMPE,
	             ModelStackWithNoteRow* engineStack)
	    : address_(address), clearAutomation_(clearAutomation), clearSequenceAndMPE_(clearSequenceAndMPE),
	      engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	bool clearAutomation_;
	bool clearSequenceAndMPE_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
