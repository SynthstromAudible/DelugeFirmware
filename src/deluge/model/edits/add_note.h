#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include "model/iterance/iterance.h" // Iterance
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Create a note in a melodic note-row — reifies the pad-tap note-add. apply funnels
/// getNewAction(NOTE_EDIT)+NoteRow::attemptNoteAdd (which records the note-array undo into the Action).
class AddNote final : public Edit {
public:
	AddNote(std::optional<project::NoteRowAddress> address, int32_t pos, int32_t length, int32_t velocity,
	        int32_t probability, Iterance iterance, int32_t fill, ModelStackWithNoteRow* engineStack)
	    : address_(address), pos_(pos), length_(length), velocity_(velocity), probability_(probability),
	      iterance_(iterance), fill_(fill), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

	/// Whether the note was actually added (false when the engine refused — e.g. an overlapping note). Valid
	/// after apply(); mirrors NoteRow::attemptNoteAdd's return so call sites keep their existing success flow.
	[[nodiscard]] bool added() const { return added_; }

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t pos_, length_, velocity_, probability_;
	Iterance iterance_;
	int32_t fill_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
	bool added_ = false;
};

} // namespace deluge::edits
