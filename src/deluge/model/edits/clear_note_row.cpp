#include "model/edits/clear_note_row.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error ClearNoteRow::apply(Context& /*ctx*/) {
	Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);
	NoteRow* noteRow = engineStack_->getNoteRow();
	noteRow->clear(action, engineStack_, clearAutomation_, clearSequenceAndMPE_);
	return Error::NONE;
}

} // namespace deluge::edits
