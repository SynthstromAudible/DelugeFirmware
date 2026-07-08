#include "model/edits/clear_clip.h"

#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"

namespace deluge::edits {

Error ClearClip::apply(Context& /*ctx*/) {
	Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);
	auto* clip = static_cast<Clip*>(engineStack_->getTimelineCounter());
	clip->clear(action, engineStack_, clearAutomation_, clearSequenceAndMPE_);
	return Error::NONE;
}

} // namespace deluge::edits
