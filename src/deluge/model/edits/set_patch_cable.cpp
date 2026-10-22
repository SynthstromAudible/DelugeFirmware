#include "model/edits/set_patch_cable.h"

#include "model/edits/set_param_value.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/patch/patch_cable.h"

namespace deluge::edits {

Error SetPatchCable::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::cableStackAt(ctx.song, cable_, polarity_, memory);
	if (stack == nullptr) {
		return Error::UNSPECIFIED;
	}
	// The strength write binds the sound's cable set to the record where it is not yet, which copies the new cable, its
	// polarity with it.
	SetParamValue write{cable_, value_};
	return deluge::edits::apply(ctx, write);
}

} // namespace deluge::edits
