#include "model/edits/delete_param_automation.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error DeleteParamAutomation::apply(Context& /*ctx*/) {
	// Funnel the site's getNewAction(AUTOMATION_DELETE)+deleteAutomation pair. deleteAutomation records the
	// undo Consequence into the Action (recordParamChangeIfNotAlreadySnapshotted, stealData=true).
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, addition_);
	engineStack_->autoParam->deleteAutomation(action, engineStack_);
	return Error::NONE;
}

} // namespace deluge::edits
