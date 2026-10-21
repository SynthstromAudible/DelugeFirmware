#include "scenes/bind/setting_context.h"

namespace deluge::scenes::bind {

engine::SettingContext setting_context(const project::Song& /*model*/, project::SlotAddress slot,
                                       const RuntimeFacts& facts) {
	using Owner = engine::SettingContext::Owner;
	using Instrument = engine::SettingContext::Instrument;
	const bool lane = slot.kind == project::SlotAddress::Kind::DRUM;
	return {.owner = lane ? Owner::KIT_LANE : Owner::TRACK,
	        .instrument = lane ? Instrument::SOUND_DRUM : Instrument::SYNTH,
	        .audio_inputs_present = facts.audio_inputs_present(),
	        .loaded_sources = facts.loaded_sources(slot)};
}

engine::SettingContext setting_context(project::ChainAddress /*chain*/, ChainOwner owner) {
	using Owner = engine::SettingContext::Owner;
	using Instrument = engine::SettingContext::Instrument;
	switch (owner) {
	case ChainOwner::KIT:
		return {.owner = Owner::KIT, .instrument = Instrument::KIT};
	case ChainOwner::AUDIO_TRACK:
		return {.owner = Owner::TRACK, .instrument = Instrument::AUDIO};
	case ChainOwner::SONG:
		break;
	}
	return {.owner = Owner::SONG, .instrument = Instrument::NONE};
}

} // namespace deluge::scenes::bind
