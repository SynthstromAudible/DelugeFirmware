#include "model/edits/set_swing.h"

#include "deluge-project/song.h"
#include "model/action/action_logger.h"

namespace deluge::edits {

Error SetSwing::apply(Context& ctx) {
	int8_t before = ctx.model.groove.swing_amount;
	actionLogger.recordSwingChange(before, newSwing_);
	ctx.model.groove.swing_amount = newSwing_;
	return Error::NONE;
}

} // namespace deluge::edits
