#include "model/edits/set_dx_patch.h"

#include "instruments/classic/codec.h"
#include "instruments/classic/state_access.h"
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
	if (slot == nullptr || sound == nullptr || sound->getSynthMode() != SynthMode::DX7) {
		return Error::UNSPECIFIED;
	}
	const deluge::instruments::classic::SoundState changed =
	    deluge::instruments::classic::with_dx7_patch(sound->state(), patch_);
	const deluge::plugin::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};
	const std::array<std::string_view, 1> keys{"dx7"};
	const auto change = deluge::instruments::classic::change_to(sound->state(), changed, keys, context);
	if (const std::optional<deluge::serialization::Value> whole = deluge::bridge::typedWriteAuditBase(*sound, *slot)) {
		deluge::bridge::auditTypedWrite(
		    *sound, *slot, *whole, deluge::instruments::classic::with_state(*whole, changed, context), change, keys[0]);
	}
	Error error = change ? deluge::bridge::commitState(*sound, *change, ctx.song) : Error::UNSPECIFIED;
	if (const deluge::instruments::classic::DxPatchState* held =
	        deluge::instruments::classic::dx7_patch(sound->state());
	    held != nullptr && *held == patch_) {
		return Error::NONE; // what else the sound refused is not this Edit's
	}
	if (error == Error::NONE) {
		error = deluge::bridge::applyDx7Patch(*sound, patch_);
	}
	return error;
}

} // namespace deluge::edits
