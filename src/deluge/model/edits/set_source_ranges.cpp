#include "model/edits/set_source_ranges.h"

#include "engines/deluge/codec.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include <array>
#include <string_view>

namespace deluge::edits {

Error SetSourceRanges::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (slot == nullptr || sound == nullptr || sound->boundState() == nullptr || source_ >= kNumSources) {
		return Error::UNSPECIFIED;
	}
	deluge::instrument::SoundState changed = *sound->boundState();
	changed.sources[source_].ranges = ranges_;
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
	if (sound->boundState()->sources[source_].ranges == ranges_) {
		deluge::bridge::retuneVoices(*sound, ctx.song);
		return Error::NONE; // what else the sound refused is not this Edit's
	}
	if (error == Error::NONE) {
		// The slot cannot hold this source's ranges (an FM sound's, a source that plays no files): the runtime holds
		// them.
		error = deluge::bridge::applyRanges(*sound, source_, ranges_);
		if (error == Error::NONE) {
			deluge::bridge::retuneVoices(*sound, ctx.song);
		}
	}
	return error;
}

} // namespace deluge::edits
