#include "model/edits/multiply_clip.h"

#include "memory/heaps.h" // alloc_sdram
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/model_stack.h"
#include "model/song/song.h"

namespace deluge::edits {

Error MultiplyClip::apply(Context& ctx) {
	action_ = actionLogger.getNewAction(ActionType::CLIP_MULTIPLY, ActionAddition::NOT_ALLOWED);

	// Record the multiply Consequence BEFORE the mutation: on redo, the note changes/deletions doubleClipLength adds
	// must run after it (they end up no-ops, but ordering matters). Verbatim from the call site.
	if (action_ != nullptr) {
		void* consMemory = deluge::memory::alloc_sdram(sizeof(ConsequenceInstrumentClipMultiply));
		if (consMemory != nullptr) {
			auto* consequence = new (consMemory) ConsequenceInstrumentClipMultiply();
			action_->addConsequence(consequence);
		}
	}

	auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());
	ctx.song.doubleClipLength(clip, action_);
	return Error::NONE;
}

} // namespace deluge::edits
