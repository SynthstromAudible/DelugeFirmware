#include "model/edits/repeat_note_edit.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error RepeatNoteEdit::apply(Context& /*ctx*/) {
	// See if we can do a "secret UNDO" — when the last open Action was the equal-and-opposite repeat edit, peel one
	// Consequence per note-row off it rather than recording a new change.
	Action* lastAction = actionLogger.firstAction[BEFORE];
	if (lastAction != nullptr && lastAction->type == ActionType::NOTE_REPEAT_EDIT && lastAction->openForAdditions
	    && lastAction->offset == -offset_) {
		// The original passed the ModelStackWithTimelineCounter's toWithSong() here; undoJustOneConsequencePerNoteRow
		// only reads ->song (the note-array reverts resolve the clip via song->getCurrentClip()), so a fresh
		// song-only stack is equivalent (ModelStackWithNoteRow doesn't inherit toWithSong()).
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* songStack = setupModelStackWithSong(modelStackMemory, engineStack_->song);
		actionLogger.undoJustOneConsequencePerNoteRow(songStack);
		return Error::NONE;
	}

	Action* action = actionLogger.getNewAction(ActionType::NOTE_REPEAT_EDIT, ActionAddition::ALLOWED);
	if (action) {
		action->offset = offset_;
	}
	engineStack_->getNoteRow()->editNoteRepeatAcrossAllScreens(squareStart_, squareWidth_, engineStack_, action,
	                                                           wrapEditLevel_, newNumNotes_);
	return Error::NONE;
}

} // namespace deluge::edits
