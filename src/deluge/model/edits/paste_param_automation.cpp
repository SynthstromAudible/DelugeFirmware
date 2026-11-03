#include "model/edits/paste_param_automation.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"
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
	settleCableDepth(stack);
	return Error::NONE;
}

} // namespace deluge::edits
