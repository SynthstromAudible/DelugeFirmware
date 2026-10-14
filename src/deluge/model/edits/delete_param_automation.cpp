#include "model/edits/delete_param_automation.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error DeleteParamAutomation::apply(Context& /*ctx*/) {
	// Open the AUTOMATION_DELETE Action, then deleteAutomation, which records the undo Consequence into it
	// (recordParamChangeIfNotAlreadySnapshotted, stealData=true).
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, addition_);
	engineStack_->autoParam->deleteAutomation(action, engineStack_);
	return Error::NONE;
}

} // namespace deluge::edits
