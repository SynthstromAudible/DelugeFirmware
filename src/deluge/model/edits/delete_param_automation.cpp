#include "model/edits/delete_param_automation.h"

#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error DeleteParamAutomation::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = engineStack_;
	if (stack == nullptr) {
		stack = address_ ? deluge::bridge::paramStackAt(ctx.song, *address_, memory) : nullptr;
		if (stack == nullptr) {
			return Error::UNSPECIFIED;
		}
	}
	// Open the AUTOMATION_DELETE Action, then deleteAutomation, which records the undo Consequence into it
	// (recordParamChangeIfNotAlreadySnapshotted, stealData=true).
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, addition_);
	stack->autoParam->deleteAutomation(action, stack);
	return Error::NONE;
}

} // namespace deluge::edits
