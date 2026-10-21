#include "model/edits/set_kit_setting.h"

#include "engines/deluge/settings.h"
#include "model/drum/drum.h"
#include "model/edits/set_engine_setting.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"

namespace deluge::edits {

namespace {

Kit* kitWithId(::Song& song, uint32_t id) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->id() == id && output->type == OutputType::KIT) {
			return static_cast<Kit*>(output);
		}
	}
	return nullptr;
}

/// Whether @p rule holds for @p sound, read in its context as a kit row: the host contract's KIT_LANE playing a
/// SOUND_DRUM, its own sources and the plugged inputs.
bool ruleHolds(std::span<const deluge::engine::SettingCondition> rule, ::Sound& sound) {
	if (rule.empty()) {
		return true;
	}
	uint8_t loaded = 0;
	for (uint8_t s = 0; s < kNumSources; ++s) {
		if (sound.sources[s].hasAtLeastOneAudioFileLoaded()) {
			loaded |= static_cast<uint8_t>(1u << s);
		}
	}
	const deluge::engine::SettingContext context{.owner = deluge::engine::SettingContext::Owner::KIT_LANE,
	                                             .instrument = deluge::engine::SettingContext::Instrument::SOUND_DRUM,
	                                             .audio_inputs_present =
	                                                 AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn,
	                                             .loaded_sources = loaded};
	const deluge::engine::SettingReader reader{.read =
	                                               [](const void* self, std::string_view path) {
		                                               return deluge::instrument::held_setting(
		                                                   *static_cast<const deluge::instrument::SoundState*>(self),
		                                                   path);
	                                               },
	                                           .self = sound.boundState()};
	return deluge::engine::conditions_hold(rule, context, reader);
}

} // namespace

Error SetKitSetting::apply(Context& ctx) {
	drums_written_ = 0;
	Kit* kit = kitWithId(ctx.song, kit_output_);
	const std::optional<deluge::serialization::Value> value = deluge::instrument::canonical_setting(path_, value_);
	if (kit == nullptr || !value) {
		return Error::UNSPECIFIED;
	}
	for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		auto& sound = *static_cast<SoundDrum*>(drum);
		const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
		deluge::project::InstrumentSlot* slot = address ? ctx.model.resolveSlot(*address) : nullptr;
		if (slot == nullptr || sound.boundState() == nullptr || !ruleHolds(fan_out_when_, sound)
		    || !deluge::instrument::takes_setting(*sound.boundState(), path_)
		    || !deluge::instrument::allows_setting(*sound.boundState(), path_, *value)) {
			continue;
		}
		if (commitEngineSetting(sound, *slot, path_, *value, ctx.song) == Error::NONE) {
			drums_written_++;
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
