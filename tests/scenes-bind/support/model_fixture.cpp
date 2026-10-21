#include "support/model_fixture.h"

#include "deluge-project/doc_note_row.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/engine.h"
#include "serialization/value.h"

#include <utility>

namespace deluge::scenes::bind::test {

uint8_t FakeFacts::loaded_sources(project::SlotAddress slot) const {
	asked.push_back(slot);
	const auto found = loaded.find(slot.id);
	return found != loaded.end() ? found->second : 0;
}

const instrument::SoundState* FakeFacts::sound_state(project::SlotAddress slot) const {
	const auto found = states.find(slot.id);
	return found != states.end() ? &found->second : nullptr;
}

bool FakeFacts::allows_note_tails(project::SlotAddress slot) const {
	const auto found = note_tails.find(slot.id);
	return found == note_tails.end() || found->second;
}

uint32_t FakeFacts::chain_key(project::ChainAddress chain) {
	return chain.kind == project::ChainAddress::Kind::SONG ? UINT32_MAX : chain.id;
}

const effects::EffectStates* FakeFacts::chain_effects(project::ChainAddress chain) const {
	const auto found = chains.find(chain_key(chain));
	return found != chains.end() ? &found->second : nullptr;
}

ModelFixture::ModelFixture() {
	song.output_structure.getOrCreate(kSynth.id);
	song.drum_structure.getOrCreate(kDrum.id);
	set_state(kSynth, instrument::new_synth_state());
	set_state(kDrum, instrument::new_synth_state());
	for (const project::ChainAddress chain : {kKitChain, kAudioChain}) {
		song.output_structure.getOrCreate(chain.id).effects = project::FxChain{};
	}
	for (const project::ChainAddress chain : {kKitChain, kAudioChain, kMasterChain}) {
		facts.chains[FakeFacts::chain_key(chain)] = effects::EffectStates{};
	}
	for (const project::TrackAddress track : {kMidiTrack, kCvTrack, kAudioTrack}) {
		(void)song.output_structure.getOrCreate(track.output_id);
	}
	(void)project::write_setting(song, {project::SettingGroupId::INSTRUMENT_DEVICE, kCvTrack}, "channel",
	                             serialization::Value::makeNumber(2));
	for (const project::ClipAddress clip : {kSynthClip, kMidiClip, kKitClip}) {
		song.clip_structure.getOrCreate(clip.clip_id).values.arp.emplace();
	}
	(void)song.audio_clip_structure.getOrCreate(kAudioClip.clip_id);
	(void)song.output_structure.getOrCreate(kKit);
	song.drum_structure.getOrCreate(kSoundLane.drum_id).arp.emplace();
	for (const auto& [lane, type] :
	     {std::pair{kMidiLane, project::DrumType::MIDI}, std::pair{kGateLane, project::DrumType::GATE}}) {
		project::DocDrum& drum = song.drum_structure.getOrCreate(lane.drum_id);
		drum.drum_type = type;
		drum.arp.emplace();
	}
	project::DocNoteRow& row = song.clip_structure.getOrCreate(kKitClip.clip_id).note_rows.emplace_back();
	row.drum_id = kDrum.id;
}

void ModelFixture::set_state(project::SlotAddress slot, const instrument::SoundState& state) {
	facts.states[slot.id] = state;
	project::InstrumentSlot made{.engine = "deluge.instrument",
	                             .state = engine::StateHandle::of<instrument::DelugeInstrument>(state),
	                             .tick_magnitude = 1};
	if (slot.kind == project::SlotAddress::Kind::OUTPUT) {
		song.output_structure.getOrCreate(slot.id).instrument = std::move(made);
	}
	else {
		song.drum_structure.getOrCreate(slot.id).instrument = std::move(made);
	}
}

} // namespace deluge::scenes::bind::test
