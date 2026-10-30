#include "model/edits/set_dx_patch.h"

#include "instruments/deluge/codec.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::edits {

Error SetDxPatch::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (slot == nullptr || sound == nullptr || sound->boundState() == nullptr || source_ >= kNumSources
	    || sound->sources[source_].oscType != OscType::DX7) {
		return Error::UNSPECIFIED;
	}
	deluge::instrument::SoundState changed = *sound->boundState();
	changed.sources[source_].dx_patch = patch_;
	const deluge::serialization::Value element =
	    deluge::instrument::with_state(slot->state, changed, {.input_tick_magnitude = slot->tick_magnitude});
	Error error = deluge::bridge::commitSlot(*sound, *slot, element, ctx.song);
	if (sound->boundState()->sources[source_].dx_patch == patch_) {
		return Error::NONE; // what else of the slot the sound refused is not this Edit's, and the slot now says it
	}
	if (error == Error::NONE) {
		// The slot cannot hold an FM sound's patch: the runtime holds it.
		error = deluge::bridge::applyDxPatch(*sound, source_, patch_);
		deluge::bridge::syncSlotState(*sound, *slot, ctx.song);
	}
	return error;
}

} // namespace deluge::edits
