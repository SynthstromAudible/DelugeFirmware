#include "model/edits/paste_param_automation.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error PasteParamAutomation::apply(Context& /*ctx*/) {
	// Funnel pasteAutomation's exact sequence: open the Action, pre-snapshot the before-state (copy, not
	// steal), then delegate to AutoParam::paste.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(engineStack_, /*stealData=*/false);
	}
	engineStack_->autoParam->paste(startPos_, endPos_, scaleFactor_, engineStack_, copied_, isPatchCable_);
	return Error::NONE;
}

} // namespace deluge::edits
