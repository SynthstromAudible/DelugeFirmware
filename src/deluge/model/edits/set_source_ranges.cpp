#include "model/edits/set_source_ranges.h"

#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetSourceRanges::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (slot == nullptr || sound == nullptr || source_ >= kNumSources) {
		return Error::UNSPECIFIED;
	}
	if (Error error = deluge::bridge::applyRanges(*sound, source_, ranges_); error != Error::NONE) {
		return error;
	}
	deluge::bridge::syncSlotState(*sound, *slot, ctx.song);
	return Error::NONE;
}

} // namespace deluge::edits
