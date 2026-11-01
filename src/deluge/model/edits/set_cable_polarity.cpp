#include "model/edits/set_cable_polarity.h"

#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/mod_matrix_host.h"
#include "processing/sound/sound.h"

#include <utility>

namespace deluge::edits {

Error SetCablePolarity::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackAt(ctx.song, cable_, memory);
	if (stack == nullptr || stack->kind != deluge::modulation::params::Kind::PATCH_CABLE) {
		return Error::UNSPECIFIED;
	}
	auto* set = static_cast<PatchCableSet*>(stack->paramCollection);
	PatchSource source{};
	deluge::synth::CableTarget destination{};
	PatchCableSet::dissectParamId(stack->paramId, &destination, &source);
	const uint8_t index = set->getPatchCableIndex(source, destination);
	if (index == 255) {
		return Error::UNSPECIFIED;
	}
	ModMatrixHost matrix{*static_cast<Sound*>(stack->modControllable), *stack->paramManager};
	if (std::optional<CableEdit> edit =
	        matrix.setPolarity(index, static_cast<deluge::synth::CablePolarity>(polarity_))) {
		recordCableEdit(stack, std::move(*edit));
	}
	return Error::NONE;
}

} // namespace deluge::edits
