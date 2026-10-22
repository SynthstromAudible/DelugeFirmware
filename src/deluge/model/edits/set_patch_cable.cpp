#include "model/edits/set_patch_cable.h"

#include "deluge-project/song.h"
#include "model/edits/set_param_value.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/snapshot/param_snapshot.h"
#include "modulation/automation/auto_param.h"
#include "modulation/patch/patch_cable.h"

namespace deluge::edits {

Error SetPatchCable::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::cableStackAt(ctx.song, cable_, polarity_, memory);
	if (stack == nullptr) {
		return Error::UNSPECIFIED;
	}
	if (write_ == Write::SETUP) {
		// The value alone, into the record and the engine's cursor: no undo step, no automation.
		const int32_t oldValue = stack->autoParam->getCurrentValue();
		if (ctx.model.resolveParam(cable_) != nullptr) {
			deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, cable_, value_, 0, 0, std::nullopt);
			stack->autoParam->projectScalarFromStore(stack, oldValue, /*isPlaying=*/false);
		}
		else {
			stack->autoParam->setCurrentValueBasicForSetup(*stack->doc(), value_);
		}
		return Error::NONE;
	}
	// The strength write binds the sound's cable set to the record where it is not yet, which copies the new cable, its
	// polarity with it.
	SetParamValue write{cable_, value_};
	return deluge::edits::apply(ctx, write);
}

} // namespace deluge::edits
