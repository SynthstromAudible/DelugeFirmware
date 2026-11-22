#include "model/edits/set_engine_setting.h"

#include "effects/settings.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/words.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "plugin/types/sync.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace deluge::edits {

namespace {

/// The change the setting at @p path becoming @p value made before the typed writers: through the slot's encoding of
/// the setting's entry (with_setting), or where its menu reshapes more, of each entry that moved (change_to). The typed
/// write's audit compares against it.
std::expected<deluge::instruments::classic::SlotChange, deluge::plugin::StateError>
encodedChange(const ::Sound& sound, const deluge::instruments::classic::SoundState& before, std::string_view path,
              const deluge::serialization::Value& value, const deluge::plugin::CodecContext& context) {
	if (std::optional<deluge::instruments::classic::SoundState> reshaped =
	        deluge::bridge::stateWithSetting(sound, path, value)) {
		return deluge::instruments::classic::change_to(
		    before, *reshaped, deluge::instruments::classic::differing_entries(before, *reshaped, context), context);
	}
	return deluge::instruments::classic::with_setting(before, path, value, context);
}

} // namespace

std::expected<deluge::instruments::classic::SlotChange, deluge::plugin::StateError>
settingChange(const ::Sound& sound, std::string_view path, const deluge::serialization::Value& value) {
	const deluge::instruments::classic::SoundState& before = sound.state();
	// Where the setting's menu reshapes more (an oscillator's type or loop mode, the synth mode), the state it reshapes
	// to; else the setting's own field, written by its family's writer.
	if (std::optional<deluge::instruments::classic::SoundState> reshaped =
	        deluge::bridge::stateWithSetting(sound, path, value)) {
		std::vector<std::string_view> changed = deluge::instruments::classic::changed_entries(before, *reshaped);
		return deluge::instruments::classic::SlotChange{.state = std::move(*reshaped), .changed = std::move(changed)};
	}
	deluge::instruments::classic::SlotChange change{.state = before, .changed = {}};
	change.changed = deluge::instruments::classic::write_setting(change.state, path, value);
	return change;
}

Error commitEngineSetting(::Sound& sound, const deluge::project::InstrumentSlot* slot, std::string_view path,
                          const deluge::serialization::Value& value, ::Song& song) {
	const SynthMode from = deluge::instruments::classic::synth_mode(sound.state());
	if (!deluge::bridge::runtimeTakes(sound, path)) {
		return Error::UNSPECIFIED; // the sound has no such setting in its engine and types
	}
	const auto change = settingChange(sound, path, value);
	if (slot != nullptr && deluge::bridge::typedWriteAuditOn()) {
		const deluge::instruments::classic::SoundState& before = sound.state();
		deluge::bridge::auditSettingWrite(
		    before, path, value,
		    encodedChange(sound, before, path, value, {.input_tick_magnitude = slot->tick_magnitude}), change);
	}
	Error error = change ? deluge::bridge::commitState(sound, *change, song) : Error::UNSPECIFIED;
	// The state holds the value, whether or not a file of this sound would (an FM sound's oscillator type), and the
	// runtime has taken it; what else the sound refused is not this Edit's.
	if (const std::optional<deluge::serialization::Value> landed =
	        deluge::instruments::classic::setting_value(sound.state(), path);
	    landed && landed->str() == value.str()) {
		error = Error::NONE;
	}
	if (path == "mode") {
		// The knobs' own SetControls nests inside this Edit on purpose: the record and the engine take the remap
		// together, and lane publication counts nesting, so only the outermost Edit publishes.
		deluge::bridge::remapKnobsForMode(sound, song, from, deluge::instruments::classic::synth_mode(sound.state()));
	}
	// After the setters have projected the sound's render copy, which the reverb copies from: a render during the write
	// would otherwise clear the flag before the new timing is there to read. Whatever landed, the reverb re-reads it.
	if (deluge::effects::shapes_reverb_sidechain(path)) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	return error;
}

Error setUnslottedSetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value,
                          ::Song& song) {
	const std::optional<deluge::serialization::Value> canonical =
	    deluge::instruments::classic::canonical_setting(path, value);
	if (!canonical || !deluge::instruments::classic::takes_setting(sound.state(), path)
	    || !deluge::instruments::classic::allows_setting(sound.state(), path, *canonical)) {
		return Error::UNSPECIFIED;
	}
	return commitEngineSetting(sound, nullptr, path, *canonical, song);
}

Error SetEngineSetting::apply(Context& ctx) {
	deluge::project::InstrumentSlot* slot = ctx.model.resolveSlot(slot_);
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	const std::optional<deluge::serialization::Value> value =
	    deluge::instruments::classic::canonical_setting(path_, value_);
	if (slot == nullptr || sound == nullptr || !value
	    || !deluge::instruments::classic::takes_setting(sound->state(), path_)
	    || !deluge::instruments::classic::allows_setting(sound->state(), path_, *value)) {
		return Error::UNSPECIFIED;
	}
	return commitEngineSetting(*sound, slot, path_, *value, ctx.song);
}

} // namespace deluge::edits
