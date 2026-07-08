#include "model/edits/nudge_note.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "modulation/params/param_manager.h"

namespace deluge::edits {

Error NudgeNote::apply(Context& /*ctx*/) {
	NoteRow* noteRow = engineStack_->getNoteRow();
	auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());

	// Shared NOTE_NUDGE Action: the first held pad's Edit opens it, subsequent pads coalesce into it via ALLOWED
	// (matching the original, which created the Action once before its pad loop).
	Action* action = actionLogger.getNewAction(ActionType::NOTE_NUDGE, ActionAddition::ALLOWED);
	if (action) {
		action->offset = offset_;
	}

	// Distance to the next note must be read BEFORE the nudge (it feeds the note-row MPE move below).
	int32_t distanceTilNext = noteRow->getDistanceToNextNote(intendedPos_, engineStack_);

	Error error = noteRow->nudgeNotesAcrossAllScreens(intendedPos_, engineStack_, action, wrapEditLevel_, offset_);
	if (error != Error::NONE) {
		return error;
	}

	// Nudge automation + MPE at the NoteRow level (extends engineStack_ in place, as the original did).
	{
		ModelStackWithThreeMainThings* modelStackForNoteRow =
		    engineStack_->addOtherTwoThingsAutomaticallyGivenNoteRow();
		noteRow->paramManager.nudgeAutomationHorizontallyAtPos(
		    intendedPos_, offset_, modelStackForNoteRow->getLoopLength(), action, modelStackForNoteRow,
		    nudgeAutomation_, nudgeMPE_, distanceTilNext);
	}

	// Nudge automation + MPE at the Clip level. The original reused the caller's parent ModelStack here (its "a bit
	// dodgy" comment) and so invalidated the caller's ModelStackWithNoteRow; build a fresh stack instead so the
	// caller's stacks survive apply() unchanged.
	{
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, engineStack_->song, clip);
		int32_t lengthBeforeLoop = clip->getLoopLength();
		ModelStackWithThreeMainThings* modelStackForClip =
		    modelStack->addOtherTwoThingsButNoNoteRow(clip->output->toModControllable(), &clip->paramManager);
		clip->paramManager.nudgeAutomationHorizontallyAtPos(intendedPos_, offset_, lengthBeforeLoop, action,
		                                                    modelStackForClip, nudgeAutomation_, nudgeMPE_);
	}
	return Error::NONE;
}

} // namespace deluge::edits
