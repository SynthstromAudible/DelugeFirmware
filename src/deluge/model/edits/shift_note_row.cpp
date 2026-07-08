#include "model/edits/shift_note_row.h"

#include "memory/heaps.h" // alloc_sdram
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/model_stack.h"

namespace deluge::edits {

Error ShiftNoteRow::apply(Context& /*ctx*/) {
	auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());
	clip->shiftOnlyOneNoteRowHorizontally(engineStack_, shiftAmount_, shiftAutomation_, shiftSequenceAndMPE_);

	// Undo record, verbatim from the call site: extend a matching open Action's Consequence amount, else open a
	// new Action + Consequence. (Goto-free restatement of the original's four cases.)
	Action* action = actionLogger.firstAction[BEFORE];
	const bool matchingAction = action != nullptr && action->type == ActionType::NOTEROW_HORIZONTAL_SHIFT
	                            && action->openForAdditions && action->currentClip == clip;
	auto* existing =
	    matchingAction ? static_cast<ConsequenceNoteRowHorizontalShift*>(action->firstConsequence) : nullptr;

	if (existing != nullptr && existing->noteRowId == engineStack_->noteRowId) {
		existing->amount += shiftAmount_; // coalesce into the open shift
		return Error::NONE;
	}
	// Need a fresh Consequence. Reuse the matching Action only when it holds no (or a different-row) Consequence
	// but is otherwise this-clip/open — i.e. open a new Action unless it's a matching Action with no Consequence.
	if (!(matchingAction && existing == nullptr)) {
		action = actionLogger.getNewAction(ActionType::NOTEROW_HORIZONTAL_SHIFT, ActionAddition::NOT_ALLOWED);
	}
	if (action != nullptr) {
		void* consMemory = deluge::memory::alloc_sdram(sizeof(ConsequenceNoteRowHorizontalShift));
		if (consMemory != nullptr) {
			auto* consequence = new (consMemory) ConsequenceNoteRowHorizontalShift(
			    engineStack_->noteRowId, shiftAmount_, shiftAutomation_, shiftSequenceAndMPE_);
			action->addConsequence(consequence);
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
