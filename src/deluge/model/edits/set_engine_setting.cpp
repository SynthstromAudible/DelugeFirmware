#include "model/edits/set_engine_setting.h"

#include "effects/settings.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/words.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "plugin-api/types/sync.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace deluge::edits {

namespace {

/// The change the setting at @p path becoming @p value made before the typed writers: through the slot's encoding of
/// the setting's entry (with_setting), or where its menu reshapes more, of each entry that moved (change_to). The typed
/// write's audit compares against it.
std::expected<deluge::instrument::SlotChange, deluge::plugin::StateError>
encodedChange(const ::Sound& sound, const deluge::instrument::SoundState& before, std::string_view path,
              const deluge::serialization::Value& value, const deluge::plugin::CodecContext& context) {
	if (std::optional<deluge::instrument::SoundState> reshaped = deluge::bridge::stateWithSetting(sound, path, value)) {
		return deluge::instrument::change_to(
		    before, *reshaped, deluge::instrument::differing_entries(before, *reshaped, context), context);
	}
	return deluge::instrument::with_setting(before, path, value, context);
}

} // namespace

std::expected<deluge::instrument::SlotChange, deluge::plugin::StateError>
settingChange(const ::Sound& sound, const deluge::project::InstrumentSlot& /*slot*/, std::string_view path,
              const deluge::serialization::Value& value) {
	const deluge::instrument::SoundState& before = *sound.boundState();
	// Where the setting's menu reshapes more (an oscillator's type or loop mode, the synth mode), the state it reshapes
	// to; else the setting's own field, written by its family's writer.
	if (std::optional<deluge::instrument::SoundState> reshaped = deluge::bridge::stateWithSetting(sound, path, value)) {
		std::vector<std::string_view> changed = deluge::instrument::changed_entries(before, *reshaped);
		return deluge::instrument::SlotChange{.state = std::move(*reshaped), .changed = std::move(changed)};
	}
	deluge::instrument::SlotChange change{.state = before, .changed = {}};
	change.changed = deluge::instrument::write_setting(change.state, path, value);
	return change;
}

Error commitEngineSetting(::Sound& sound, deluge::project::InstrumentSlot& slot, std::string_view path,
                          const deluge::serialization::Value& value, ::Song& song) {
	const auto change = settingChange(sound, slot, path, value);
	if (deluge::bridge::typedWriteAudit().enabled) {
		const deluge::instrument::SoundState& before = *sound.boundState();
		deluge::bridge::auditSettingWrite(
		    before, path, value,
		    encodedChange(sound, before, path, value, {.input_tick_magnitude = slot.tick_magnitude}), change);
	}
	Error error = change ? deluge::bridge::commitState(sound, *change, song) : Error::UNSPECIFIED;
	const std::optional<deluge::serialization::Value> landed =
	    deluge::instrument::setting_value(*sound.boundState(), path);
	if (!landed || landed->str() != value.str()) {
		if (error == Error::NONE) {
			// The state does not read this value back (a path that does not apply to this sound): the runtime
			// takes it as its setter does.
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
