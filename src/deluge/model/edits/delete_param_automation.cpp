#include "model/edits/delete_param_automation.h"

#include "model/action/action_logger.h"
#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error DeleteParamAutomation::apply(Context& ctx) {
	const std::optional<bridge::LaneTarget> lane = lane_for(ctx.song, address_, resolve_);
	if (!lane) {
		return Error::UNSPECIFIED;
	}
	// The automation edits still take an engine stack.
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = bridge::stack_of(*lane, memory);
	// Open the AUTOMATION_DELETE Action, then deleteAutomation, which records the undo Consequence into it
	// (recordParamChangeIfNotAlreadySnapshotted, stealData=true).
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, addition_);
	stack->autoParam->deleteAutomation(action, stack);
	settleCableDepth(*lane);
	return Error::NONE;
}

} // namespace deluge::edits
