#include "model/edits/record_expression_value.h"

#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"

namespace deluge::edits {

Error RecordExpressionValue::apply(Context& ctx) {
	const std::optional<bridge::LaneTarget> lane = bridge::lane_exact(ctx.song, address_);
	if (!lane) {
		return Error::UNSPECIFIED;
	}
	// No node pruning in a linear run: the value may be sent on as MIDI.
	lane->auto_param->setCurrentValueInResponseToUserInput(value_, *lane, true, livePos_, false, mpeMode_);
	return Error::NONE;
}

} // namespace deluge::edits
