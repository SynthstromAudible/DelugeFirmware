#include "model/edits/set_euclidean_events.h"

#include "definitions_cxx.hpp" // kDefaultLiftValue
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/note/note_vector.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

namespace deluge::edits {

Error SetEuclideanEvents::apply(Context& /*ctx*/) {
	// Faithful reification of InstrumentClipView::editNumEuclideanEvents' mutating tail. The view has already
	// clamped newNumNotes and guaranteed a non-null note-row; here we reproduce its two paths exactly.
	NoteRow* noteRow = engineStack_->getNoteRowAllowNull();
	auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());

	// Do a "partial undo" if we can — when the last open Action was the equal-and-opposite euclidean edit, peel one
	// Consequence per note-row off it rather than recording a new change. No need to check clip/openForAdditions
	// beyond this: the Action is closed manually when auditioning stops.
	Action* lastAction = actionLogger.firstAction[BEFORE];
	if (lastAction != nullptr && lastAction->type == ActionType::EUCLIDEAN_NUM_EVENTS_EDIT
	    && lastAction->openForAdditions && lastAction->offset == -offset_) {

		char modelStackMemory2[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStackWithJustSong = setupModelStackWithSong(modelStackMemory2, engineStack_->song);

		bool revertedWholeAction = actionLogger.undoJustOneConsequencePerNoteRow(modelStackWithJustSong);
		if (!revertedWholeAction) {
			// Only one Consequence was peeled — the note row changed, so play it. (When the whole Action reverted,
			// the original fell straight through to the display with no expectEvent; preserve that.)
			clip->expectEvent();
		}
		return Error::NONE;
	}

	// Otherwise rebuild the note pattern from scratch. Make a fresh NoteVector for the new Notes, since the
	// ActionLogger will be "stealing" the old data.
	NoteVector newNotes;
	if (newNumNotes_) {
		Error error = newNotes.insertAt(0, newNumNotes_).error_or(Error::NONE); // Pre-allocate, so no errors later
		if (error != Error::NONE) {
			return error;
		}
	}

	Action* action = actionLogger.getNewAction(ActionType::EUCLIDEAN_NUM_EVENTS_EDIT, ActionAddition::ALLOWED);
	if (action) {
		action->offset = offset_;
	}

	// Create the Notes
	for (int32_t n = 0; n < newNumNotes_; n++) {
		Note* note = &newNotes[n];
		note->pos = (uint32_t)(n * numStepsAvailable_) / (uint32_t)newNumNotes_ * squareWidth_;
		note->length = squareWidth_;
		note->probability = noteRow->getDefaultProbability();
		note->velocity = ((Instrument*)clip->output)->defaultVelocity;
		note->lift = kDefaultLiftValue;
		note->iterance = noteRow->getDefaultIterance();
		note->fill = noteRow->getDefaultFill(engineStack_);
	}

	// Just make sure final note isn't too long
	if (newNumNotes_) {
		Note* note = &newNotes[newNumNotes_ - 1];
		int32_t maxLength = effectiveLength_ - note->pos;
		if (note->length > maxLength) {
			note->length = maxLength;
		}
	}

	// Delete / steal / consequence-ize the MPE data first, because in order for partial undos to work, this has to
	// be further down the linked list of Consequences than the note-array-change that we do next, below.
	ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
	ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
	if (mpeParams) {
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    engineStack_->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams, mpeParamsSummary);
		mpeParams->deleteAllAutomation(action, modelStackWithParamCollection);
	}

	// Record change, stealing the old note data. We "definitely" store the change, because unusually, we may want to
	// revert individual Consequences in the Action one by one.
	if (action) {
		action->recordNoteArrayChangeDefinitely(clip, engineStack_->noteRowId, &noteRow->notes, true);
	}

	// Swap the new temporary note data into the permanent place
	noteRow->notes.swap(newNotes);

#if ENABLE_SEQUENTIALITY_TESTS
	noteRow->notes.testSequentiality("E376");
#endif

	// Play it
	clip->expectEvent();
	return Error::NONE;
}

} // namespace deluge::edits
