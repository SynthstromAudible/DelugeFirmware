#include "model/edits/paste_param_automation.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/automation/record_events.h"
#include "modulation/params/param.h"
#include "project/param_edit.h"

namespace deluge::edits {

Error PasteParamAutomation::apply(Context& ctx) {
	const std::optional<bridge::LaneTarget> lane = lane_for(ctx.song, address_, resolve_);
	if (!lane || lane->auto_param == nullptr) {
		return Error::UNSPECIFIED;
	}
	// The automation edits take an engine stack.
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = bridge::stack_of(*lane, memory);
	const bool isPatchCable = stack->paramCollection->getParamKind() == modulation::params::Kind::PATCH_CABLE;
	// Open the Action, pre-snapshot the before-state (copy, not steal), then delegate to AutoParam::paste.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(stack, /*stealData=*/false);
	}
	stack->autoParam->paste(startPos_, endPos_, scaleFactor_, stack, copied_, isPatchCable);
	if (copied_->nodes != nullptr && copied_->numNodes != 0) {
		deluge::modulation::postLaneCommand(
		    *lane,
		    {.kind = deluge::modulation::LaneCommand::Kind::PASTE,
		     .pos = startPos_,
		     .endPos = endPos_,
		     .loopLength = stack->getLoopLength(),
		     .scale = scaleFactor_,
		     .isPatchCable = isPatchCable,
		     .pasted = {.width = copied_->width,
		                .nodes = std::vector<ParamNode>(copied_->nodes, copied_->nodes + copied_->numNodes)}},
		    static_cast<size_t>(copied_->numNodes) + 2);
	}
	settleCableDepth(*lane);
	return Error::NONE;
}

} // namespace deluge::edits
