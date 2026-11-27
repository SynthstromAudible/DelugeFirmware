#include "model/edits/set_cable_polarity.h"

#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_selection.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/mod_matrix_host.h"
#include "processing/sound/sound.h"

#include <utility>

namespace deluge::edits {

Error SetCablePolarity::apply(Context& ctx) {
	const std::optional<bridge::LaneTarget> lane = bridge::lane_at(ctx.song, cable_);
	Sound* const* sound = lane ? std::get_if<Sound*>(&lane->owner) : nullptr;
	if (sound == nullptr || !bridge::isCableCollection(lane->collection)) {
		return Error::UNSPECIFIED;
	}
	auto* set = static_cast<PatchCableSet*>(lane->collection);
	PatchSource source{};
	deluge::synth::CableTarget destination{};
	PatchCableSet::dissectParamId(lane->param_id, &destination, &source);
	const uint8_t index = set->getPatchCableIndex(source, destination);
	if (index == 255) {
		return Error::UNSPECIFIED;
	}
	ModMatrixHost matrix{**sound, *lane->manager};
	if (std::optional<CableEdit> edit =
	        matrix.setPolarity(index, static_cast<deluge::synth::CablePolarity>(polarity_))) {
		// The undo record is made from an engine stack.
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		recordCableEdit(bridge::owner_stack_of(*lane, memory), std::move(*edit));
	}
	return Error::NONE;
}

} // namespace deluge::edits
