#include "model/edits/record_expression_value.h"

#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error RecordExpressionValue::apply(Context& ctx) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* lane = bridge::paramStackExact(ctx.song, address_, memory);
	if (lane == nullptr) {
		return Error::UNSPECIFIED;
	}
	// No node pruning in a linear run: the value may be sent on as MIDI.
	lane->autoParam->setCurrentValueInResponseToUserInput(value_, lane, true, livePos_, false, mpeMode_);
	return Error::NONE;
}

} // namespace deluge::edits
