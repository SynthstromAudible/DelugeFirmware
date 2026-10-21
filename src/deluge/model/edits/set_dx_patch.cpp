#include "model/edits/set_dx_patch.h"

#include "engines/deluge/codec.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include <array>
#include <string_view>

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
	const deluge::engine::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};
	const std::array<std::string_view, 1> keys{source_ == 0 ? "osc1" : "osc2"};
	const auto change = deluge::instrument::change_to(*sound->boundState(), changed, keys, context);
	deluge::bridge::auditTypedWrite(
	    *sound, *slot,
	    [&](const deluge::serialization::Value& whole) {
		    return deluge::instrument::with_state(whole, changed, context);
	    },
	    change, keys[0]);
	Error error = change ? deluge::bridge::commitState(*sound, *change, ctx.song) : Error::UNSPECIFIED;
	if (sound->boundState()->sources[source_].dx_patch == patch_) {
		return Error::NONE; // what else the sound refused is not this Edit's
	}
	if (error == Error::NONE) {
		// The slot cannot hold an FM sound's patch: the runtime holds it.
		error = deluge::bridge::applyDxPatch(*sound, source_, patch_);
	}
	return error;
}

} // namespace deluge::edits
