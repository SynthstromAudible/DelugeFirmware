#include "model/edits/paste_param_automation.h"

#include "deluge-project/param_edit.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/automation/record_events.h"
#include "modulation/params/param.h"

namespace deluge::edits {

Error PasteParamAutomation::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = paramStackFor(ctx.song, address_, resolve_, memory);
	if (stack == nullptr || stack->autoParam == nullptr) {
		return Error::UNSPECIFIED;
	}
	const bool isPatchCable = stack->paramCollection->getParamKind() == modulation::params::Kind::PATCH_CABLE;
	// Open the Action, pre-snapshot the before-state (copy, not steal), then delegate to AutoParam::paste.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(stack, /*stealData=*/false);
	}
	stack->autoParam->paste(startPos_, endPos_, scaleFactor_, stack, copied_, isPatchCable);
	if (copied_->nodes != nullptr && copied_->numNodes != 0) {
		const deluge::project::CopiedAutomation pasted{
		    .width = copied_->width,
		    .nodes = std::vector<ParamNode>(copied_->nodes, copied_->nodes + copied_->numNodes),
		};
		deluge::modulation::postLaneCommand(*stack,
		                                    {.kind = deluge::modulation::RecordEvent::Kind::PASTE,
		                                     .effectiveLength = stack->getLoopLength(),
		                                     .pos = startPos_,
		                                     .endPos = endPos_,
		                                     .scale = scaleFactor_,
		                                     .isPatchCable = isPatchCable,
		                                     .pasted = &pasted},
		                                    static_cast<size_t>(copied_->numNodes) + 2);
	}
	settleCableDepth(stack);
	return Error::NONE;
}

} // namespace deluge::edits
