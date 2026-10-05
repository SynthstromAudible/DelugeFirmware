#include "model/edits/set_source_ranges.h"

#include "engines/deluge/codec.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::edits {

Error SetSourceRanges::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (slot == nullptr || sound == nullptr || sound->boundState() == nullptr || source_ >= kNumSources) {
		return Error::UNSPECIFIED;
	}
	deluge::instrument::SoundState changed = *sound->boundState();
	changed.sources[source_].ranges = ranges_;
	const deluge::serialization::Value element =
	    deluge::instrument::with_state(slot->state, changed, {.input_tick_magnitude = slot->tick_magnitude});
	Error error = deluge::bridge::commitSlot(*sound, *slot, element, ctx.song);
	if (sound->boundState()->sources[source_].ranges == ranges_) {
		return Error::NONE; // what else of the slot the sound refused is not this Edit's, and the slot now says it
	}
	if (error == Error::NONE) {
		// The slot cannot hold this source's ranges (an FM sound's, a source that plays no files): the runtime holds
		// them.
		error = deluge::bridge::applyRanges(*sound, source_, ranges_);
		deluge::bridge::syncSlotState(*sound, *slot, ctx.song);
	}
	return error;
}

} // namespace deluge::edits
