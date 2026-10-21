#include "model/edits/set_engine_setting.h"

#include "engine-api/types/sync.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "engines/deluge/words.h"
#include "engines/effects/settings.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "serialization/tree.h"

namespace deluge::edits {

namespace {

/// The tree a slot holding @p whole takes when @p sound's setting at @p path becomes @p value: the leaf in its file
/// form (an LFO's sync level relative to the slot's magnitude), or where the setting's menu reshapes more the entries
/// of the state it reaches. The typed write's audit compares against it.
deluge::serialization::Value elementFor(const ::Sound& sound, const deluge::serialization::Value& whole,
                                        int32_t magnitude, std::string_view path,
                                        const deluge::serialization::Value& value) {
	if (std::optional<deluge::instrument::SoundState> reshaped = deluge::bridge::stateWithSetting(sound, path, value)) {
		return deluge::instrument::with_state(whole, *reshaped, {.input_tick_magnitude = magnitude});
	}
	if (path.ends_with("/syncLevel")) {
		const int32_t level = deluge::serialization::int_of(&value).value_or(0);
		return deluge::bridge::withLeaf(
		    whole, path,
		    deluge::serialization::Value::makeNumber(deluge::engine::sync_level_to_file(level, magnitude)));
	}
	return deluge::bridge::withLeaf(whole, path, value);
}

} // namespace

std::expected<deluge::instrument::SlotChange, deluge::engine::StateError>
settingChange(const ::Sound& sound, const deluge::project::InstrumentSlot& slot, std::string_view path,
              const deluge::serialization::Value& value) {
	const deluge::engine::CodecContext context{.input_tick_magnitude = slot.tick_magnitude};
	const deluge::instrument::SoundState& before = *sound.boundState();
	// Through the setting's own entry, or where its menu reshapes more (an oscillator's type or loop mode, the synth
	// mode) through each entry that moved.
	if (std::optional<deluge::instrument::SoundState> reshaped = deluge::bridge::stateWithSetting(sound, path, value)) {
		return deluge::instrument::change_to(
		    before, *reshaped, deluge::instrument::differing_entries(before, *reshaped, context), context);
	}
	return deluge::instrument::with_setting(before, path, value, context);
}

Error commitEngineSetting(::Sound& sound, deluge::project::InstrumentSlot& slot, std::string_view path,
                          const deluge::serialization::Value& value, ::Song& song) {
	const auto change = settingChange(sound, slot, path, value);
	if (const std::optional<deluge::serialization::Value> whole = deluge::bridge::typedWriteAuditBase(sound, slot)) {
		deluge::bridge::auditTypedWrite(sound, slot, *whole,
		                                elementFor(sound, *whole, slot.tick_magnitude, path, value), change, path);
	}
	Error error = change ? deluge::bridge::commitState(sound, *change, song) : Error::UNSPECIFIED;
	const std::optional<deluge::serialization::Value> landed =
	    deluge::instrument::setting_value(*sound.boundState(), path);
	if (!landed || landed->str() != value.str()) {
		if (error == Error::NONE) {
			// The slot cannot hold this value (a live input's interpolation, a sync level the magnitude cannot
			// write): the runtime holds it until a save and reload.
			error = deluge::bridge::applySetting(sound, path, value, song);
		}
	}
	else {
		error = Error::NONE; // what else the sound refused is not this Edit's
	}
	// After the setters have projected the sound's render copy, which the reverb copies from: a render during the write
	// would otherwise clear the flag before the new timing is there to read. Whatever landed, the reverb re-reads it.
	if (deluge::effects::shapes_reverb_sidechain(path)) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	return error;
}

Error SetEngineSetting::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	const std::optional<deluge::serialization::Value> value = deluge::instrument::canonical_setting(path_, value_);
	if (slot == nullptr || sound == nullptr || sound->boundState() == nullptr || !value
	    || !deluge::instrument::takes_setting(*sound->boundState(), path_)
	    || !deluge::instrument::allows_setting(*sound->boundState(), path_, *value)) {
		return Error::UNSPECIFIED;
	}
	return commitEngineSetting(*sound, *slot, path_, *value, ctx.song);
}

} // namespace deluge::edits
