#include "model/edits/shift_clip.h"

#include "memory/heaps.h" // alloc_sdram
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/consequence/consequence_clip_horizontal_shift.h"
#include "model/model_stack.h"

namespace deluge::edits {

Error ShiftClip::apply(Context& /*ctx*/) {
	auto* clip = static_cast<Clip*>(engineStack_->getTimelineCounter());

	shifted_ = clip->shiftHorizontally(engineStack_, shiftAmount_, shiftAutomation_, shiftSequenceAndMPE_);
	if (!shifted_) {
		return Error::NONE; // Nothing moved; the view bails without rendering or recording.
	}

	// Undo record, verbatim from the call site (goto-free restatement): extend a matching open Action's Consequence
	// amount; else reuse a matching consequence-less Action or open a new one, and add a fresh Consequence.
	Action* action = actionLogger.firstAction[BEFORE];
	const bool matchingAction = action != nullptr && action->type == ActionType::CLIP_HORIZONTAL_SHIFT
	                            && action->openForAdditions && action->currentClip == clip;

	if (matchingAction && action->firstConsequence != nullptr) {
		static_cast<ConsequenceClipHorizontalShift*>(action->firstConsequence)->amount += shiftAmount_;
		return Error::NONE;
	}
	// Need a fresh Consequence: reuse the matching (consequence-less) Action, else open a new one.
	if (!matchingAction) {
		action = actionLogger.getNewAction(ActionType::CLIP_HORIZONTAL_SHIFT, ActionAddition::NOT_ALLOWED);
	}
	if (action != nullptr) {
		void* consMemory = deluge::memory::alloc_sdram(sizeof(ConsequenceClipHorizontalShift));
		if (consMemory != nullptr) {
			auto* consequence =
			    new (consMemory) ConsequenceClipHorizontalShift(shiftAmount_, shiftAutomation_, shiftSequenceAndMPE_);
			action->addConsequence(consequence);
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
