#include "model/edits/add_note.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error AddNote::apply(Context& /*ctx*/) {
	NoteRow* noteRow = engineStack_->getNoteRow();
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	// attemptNoteAdd records the note-array undo Consequence into `action` itself.
	noteRow->attemptNoteAdd(pos_, length_, velocity_, probability_, iterance_, fill_, engineStack_, action);
	return Error::NONE;
}

} // namespace deluge::edits
