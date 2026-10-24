#include "model/edits/clear_clip.h"

#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"

namespace deluge::edits {

Error ClearClip::apply(Context& ctx) {
	Clip* clip = ctx.song.clipForId(address_.clip_id);
	if (clip == nullptr) {
		return Error::UNSPECIFIED;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, &ctx.song, clip);
	Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);
	clip->clear(action, stack, clearAutomation_, clearSequenceAndMPE_);
	return Error::NONE;
}

} // namespace deluge::edits
