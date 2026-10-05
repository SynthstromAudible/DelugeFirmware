#include "model/edits/set_dx_patch.h"

#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetDxPatch::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (slot == nullptr || sound == nullptr || source_ >= kNumSources) {
		return Error::UNSPECIFIED;
	}
	if (Error error = deluge::bridge::applyDxPatch(*sound, source_, patch_); error != Error::NONE) {
		return error;
	}
	deluge::bridge::syncSlotState(*sound, *slot, ctx.song);
	return Error::NONE;
}

} // namespace deluge::edits
