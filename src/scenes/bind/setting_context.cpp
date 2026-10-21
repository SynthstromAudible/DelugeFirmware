#include "scenes/bind/setting_context.h"

#include <variant>

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

engine::SettingContext setting_context(const project::Song& model, const project::SettingAddress& address,
                                       engine::SettingContext::Instrument instrument, const RuntimeFacts& facts) {
	using Owner = engine::SettingContext::Owner;
	using Instrument = engine::SettingContext::Instrument;
	using project::SettingGroupId;
	// A kit lane is played by its drum, which its record names.
	const auto lane_of = [&](uint32_t drum_id) -> engine::SettingContext {
		const project::DocDrum* drum = model.drum_structure.find(drum_id);
		const project::DrumType type = drum != nullptr ? drum->drum_type : project::DrumType::SOUND;
		return {.owner = Owner::KIT_LANE,
		        .instrument = type == project::DrumType::MIDI   ? Instrument::MIDI_DRUM
		                      : type == project::DrumType::GATE ? Instrument::GATE_DRUM
		                                                        : Instrument::SOUND_DRUM};
	};
	engine::SettingContext context{.owner = Owner::CLIP, .instrument = instrument};
	switch (address.group) {
	case SettingGroupId::SONG:
		context = {.owner = Owner::SONG, .instrument = Instrument::NONE};
		break;
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE:
	case SettingGroupId::AUDIO_INPUT:
		context = {.owner = Owner::TRACK, .instrument = instrument};
		break;
	case SettingGroupId::LANE:
	case SettingGroupId::LANE_ARP:
		if (const auto* lane = std::get_if<project::LaneAddress>(&address.owner)) {
			context = lane_of(lane->drum_id);
		}
		break;
	case SettingGroupId::NOTE_ROW:
		if (const auto* row = std::get_if<project::NoteRowAddress>(&address.owner); row != nullptr && row->is_kit) {
			context = lane_of(static_cast<uint32_t>(row->row_key));
		}
		break;
	case SettingGroupId::AUDIO_CLIP:
		context = {.owner = Owner::CLIP, .instrument = Instrument::AUDIO};
		break;
	case SettingGroupId::CLIP:
	case SettingGroupId::CLIP_VALUES:
	case SettingGroupId::CLIP_ARP:
		break;
	}
	context.audio_inputs_present = facts.audio_inputs_present();
	return context;
}

} // namespace deluge::scenes::bind
