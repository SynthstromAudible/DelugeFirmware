#include "model/edits/remove_note.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error RemoveNote::apply(Context& /*ctx*/) {
	NoteRow* noteRow = engineStack_->getNoteRow();
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	noteRow->deleteNoteByPos(engineStack_, pos_, action); // finds the note at pos; records the note-array undo
	return Error::NONE;
}

} // namespace deluge::edits
