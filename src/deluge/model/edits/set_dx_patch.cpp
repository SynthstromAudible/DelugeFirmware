#include "model/edits/set_dx_patch.h"

#include "instruments/deluge/codec.h"
#include "instruments/deluge/state_access.h"
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
	if (slot == nullptr || sound == nullptr || source_ >= kNumSources || sound->oscType(source_) != OscType::DX7) {
		return Error::UNSPECIFIED;
	}
	const auto id = static_cast<deluge::instrument::SourceId>(source_);
	const deluge::instrument::SoundState changed = deluge::instrument::with_dx_patch(sound->state(), id, patch_);
	const deluge::plugin::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};
	const std::array<std::string_view, 1> keys{source_ == 0 ? "osc1" : "osc2"};
	const auto change = deluge::instrument::change_to(sound->state(), changed, keys, context);
	if (const std::optional<deluge::serialization::Value> whole = deluge::bridge::typedWriteAuditBase(*sound, *slot)) {
		deluge::bridge::auditTypedWrite(*sound, *slot, *whole, deluge::instrument::with_state(*whole, changed, context),
		                                change, keys[0]);
	}
	Error error = change ? deluge::bridge::commitState(*sound, *change, ctx.song) : Error::UNSPECIFIED;
	if (const deluge::instrument::DxPatchState* held = deluge::instrument::dx_patch(sound->state(), id);
	    held != nullptr && *held == patch_) {
		return Error::NONE; // what else the sound refused is not this Edit's
	}
	if (error == Error::NONE) {
		// The slot cannot hold an FM sound's patch: the runtime holds it.
		error = deluge::bridge::applyDxPatch(*sound, source_, patch_);
	}
	return error;
}

} // namespace deluge::edits
