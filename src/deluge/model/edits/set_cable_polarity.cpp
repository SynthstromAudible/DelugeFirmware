#include "model/edits/set_cable_polarity.h"

#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"

namespace deluge::edits {

Error SetCablePolarity::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(ctx.song, cable_, memory);
	if (stack == nullptr || stack->kind != deluge::modulation::params::Kind::PATCH_CABLE) {
		return Error::UNSPECIFIED;
	}
	// Bind the sound's cable set to the record first, so the polarity reaches it.
	(void)stack->doc();
	auto* set = static_cast<PatchCableSet*>(stack->paramCollection);
	PatchSource source{};
	ParamDescriptor destination{};
	PatchCableSet::dissectParamId(stack->paramId, &destination, &source);
	const uint8_t index = set->getPatchCableIndex(source, destination, nullptr, false);
	if (index == 255) {
		return Error::UNSPECIFIED;
	}
	set->setPolarity(index, polarity_);
	return Error::NONE;
}

} // namespace deluge::edits
