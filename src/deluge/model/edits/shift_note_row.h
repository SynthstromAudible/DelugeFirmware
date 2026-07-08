#pragma once

#include "deluge-project/note_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// Shift a single note-row horizontally (the reification of InstrumentClipView's NOTEROW_HORIZONTAL_SHIFT). apply
/// does the engine shift, then records/coalesces the undo the way the call site did: it extends an open
/// same-note-row shift Action's ConsequenceNoteRowHorizontalShift amount, or opens a new Action + Consequence.
class ShiftNoteRow final : public Edit {
public:
	ShiftNoteRow(std::optional<project::NoteRowAddress> address, int32_t shiftAmount, bool shiftAutomation,
	             bool shiftSequenceAndMPE, ModelStackWithNoteRow* engineStack)
	    : address_(address), shiftAmount_(shiftAmount), shiftAutomation_(shiftAutomation),
	      shiftSequenceAndMPE_(shiftSequenceAndMPE), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::NoteRowAddress> address_;
	int32_t shiftAmount_;
	bool shiftAutomation_;
	bool shiftSequenceAndMPE_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
