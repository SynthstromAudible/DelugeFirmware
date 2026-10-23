#include "model/edits/paste_param_automation.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"

namespace deluge::edits {

Error PasteParamAutomation::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = engineStack_;
	bool isPatchCable = isPatchCable_;
	if (stack == nullptr) {
		if (address_) {
			project::ParamAddress at = *address_;
			stack = follow_take_ ? bridge::paramStackForWrite(ctx.song, at, memory)
			                     : bridge::paramStackAt(ctx.song, at, memory);
		}
		if (stack == nullptr || stack->autoParam == nullptr) {
			return Error::UNSPECIFIED;
		}
		isPatchCable = stack->paramCollection->getParamKind() == modulation::params::Kind::PATCH_CABLE;
	}
	// Open the Action, pre-snapshot the before-state (copy, not steal), then delegate to AutoParam::paste.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);
	if (action != nullptr) {
		action->recordParamChangeIfNotAlreadySnapshotted(stack, /*stealData=*/false);
	}
	stack->autoParam->paste(startPos_, endPos_, scaleFactor_, stack, copied_, isPatchCable);
	return Error::NONE;
}

} // namespace deluge::edits
