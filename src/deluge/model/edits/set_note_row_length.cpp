#include "model/edits/set_note_row_length.h"

#include "memory/heaps.h" // alloc_sdram
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/model_stack.h"

namespace deluge::edits {

Error SetNoteRowLength::apply(Context& /*ctx*/) {
	// Faithful reification of the NOTEROW_LENGTH_EDIT new-action branch: open the Action, build a
	// ConsequenceNoteRowLength keyed by the note-row id, and let performChange apply the change + record undo.
	Action* action = actionLogger.getNewAction(ActionType::NOTEROW_LENGTH_EDIT, ActionAddition::NOT_ALLOWED);
	if (action == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	void* consMemory = deluge::memory::alloc_sdram(sizeof(ConsequenceNoteRowLength));
	if (consMemory == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	auto* consequence = new (consMemory) ConsequenceNoteRowLength(engineStack_->noteRowId, newLength_);
	action->addConsequence(consequence);
	consequence->performChange(engineStack_, action, oldPos_, hadIndependentPlayPosBefore_);
	return Error::NONE;
}

} // namespace deluge::edits
