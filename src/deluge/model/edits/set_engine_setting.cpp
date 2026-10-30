#include "model/edits/set_engine_setting.h"

#include "instruments/deluge/codec.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/words.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "plugin-api/types/sync.h"
#include "processing/sound/sound.h"
#include "serialization/tree.h"

namespace deluge::edits {

namespace {

/// The slot state @p slot takes when @p sound's setting at @p path becomes @p value: the leaf in its file form (an
/// LFO's sync level relative to the slot's magnitude), or where the setting's menu reshapes more (an oscillator's type
/// or loop mode) the entries of the state it reaches.
deluge::serialization::Value elementFor(const ::Sound& sound, const deluge::project::InstrumentSlot& slot,
                                        std::string_view path, const deluge::serialization::Value& value) {
	if (std::optional<deluge::instrument::SoundState> reshaped = deluge::bridge::stateWithSetting(sound, path, value)) {
		return deluge::instrument::with_state(slot.state, *reshaped, {.input_tick_magnitude = slot.tick_magnitude});
	}
	if (path.ends_with("/syncLevel")) { // an LFO's, the delay's or the sidechain's
		const int32_t level = deluge::serialization::int_of(&value).value_or(0);
		return deluge::bridge::withLeaf(
		    slot.state, path,
		    deluge::serialization::Value::makeNumber(deluge::plugin::sync_level_to_file(level, slot.tick_magnitude)));
	}
	return deluge::bridge::withLeaf(slot.state, path, value);
}

} // namespace

Error SetEngineSetting::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	const std::optional<deluge::serialization::Value> value = deluge::instrument::canonical_setting(path_, value_);
	if (slot == nullptr || sound == nullptr || sound->boundState() == nullptr || !value
	    || !deluge::instrument::takes_setting(*sound->boundState(), path_)
	    || !deluge::instrument::allows_setting(*sound->boundState(), path_, *value)) {
		return Error::UNSPECIFIED;
	}
	Error error = deluge::bridge::commitSlot(*sound, *slot, elementFor(*sound, *slot, path_, *value), ctx.song);
	const std::optional<deluge::serialization::Value> landed =
	    deluge::instrument::setting_value(*sound->boundState(), path_);
	if (landed && landed->str() == value->str()) {
		return Error::NONE; // what else of the slot the sound refused is not this Edit's, and the slot now says it
	}
	if (error != Error::NONE) {
		return error;
	}
	// The slot cannot hold this value (a live input's interpolation, a sync level the magnitude cannot write): the
	// runtime holds it until a save and reload.
	error = deluge::bridge::applySetting(*sound, path_, *value, ctx.song);
	deluge::bridge::syncSlotState(*sound, *slot, ctx.song);
	return error;
}

} // namespace deluge::edits
