#include "model/edits/set_patch_cable.h"

#include "model/edits/set_param_value.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/snapshot/param_snapshot.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/record_events.h"
#include "modulation/patch/patch_cable.h"
#include "project/song.h"

namespace deluge::edits {

Error SetPatchCable::apply(Context& ctx) {
	const std::optional<deluge::bridge::LaneTarget> lane = deluge::bridge::cable_lane_at(ctx.song, cable_, polarity_);
	if (!lane) {
		return Error::UNSPECIFIED;
	}
	// The lane commands still take an engine stack.
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::stack_of(*lane, memory);
	if (write_ == Write::SETUP) {
		// The value alone, into the record and the engine's cursor: no undo step, no automation.
		const int32_t oldValue = stack->autoParam->getCurrentValue();
		if (ctx.model.resolveParam(cable_) != nullptr) {
			deluge::snapshot::ParamSnapshot::applyPatch(ctx.model, cable_, value_, 0, 0, std::nullopt);
			stack->autoParam->projectScalarFromStore(*lane, oldValue, /*isPlaying=*/false);
		}
		else {
			stack->autoParam->setBase(*stack->doc(), value_);
		}
		deluge::modulation::postLaneCommand(*stack,
		                                    {.kind = deluge::modulation::LaneCommand::Kind::BASE, .value = value_}, 0);
		return Error::NONE;
	}
	// The strength write binds the sound's cable set to the record where it is not yet, which copies the new cable, its
	// polarity with it.
	SetParamValue write{cable_, value_};
	return deluge::edits::apply(ctx, write);
}

} // namespace deluge::edits
