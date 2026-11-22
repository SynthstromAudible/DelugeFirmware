#include "model/edits/set_source_ranges.h"

#include "instruments/classic/codec.h"
#include "instruments/classic/state_access.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace deluge::edits {

Error SetSourceRanges::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	if (slot == nullptr || sound == nullptr || source_ >= kNumSources) {
		return Error::UNSPECIFIED;
	}
	const auto id = static_cast<deluge::instruments::classic::SourceId>(source_);
	const deluge::instruments::classic::SoundState changed =
	    deluge::instruments::classic::with_ranges(sound->state(), id, ranges_);
	const deluge::plugin::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};
	const std::array<std::string_view, 1> keys{source_ == 0 ? "osc1" : "osc2"};
	const auto change = deluge::instruments::classic::change_to(sound->state(), changed, keys, context);
	if (const std::optional<deluge::serialization::Value> whole = deluge::bridge::typedWriteAuditBase(*sound, *slot)) {
		deluge::bridge::auditTypedWrite(
		    *sound, *slot, *whole, deluge::instruments::classic::with_state(*whole, changed, context), change, keys[0]);
	}
	Error error = change ? deluge::bridge::commitState(*sound, *change, ctx.song) : Error::UNSPECIFIED;
	if (std::ranges::equal(deluge::instruments::classic::source_ranges(sound->state(), id), ranges_)) {
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
