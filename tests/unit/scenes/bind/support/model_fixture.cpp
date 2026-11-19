#include "support/model_fixture.h"

#include "deluge-project/doc_note_row.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/deluge_synth.h"
#include "serialization/value.h"

#include <utility>

namespace deluge::scenes::bind::test {

bool FakeFacts::allows_note_tails(project::SlotAddress slot, std::optional<project::ClipAddress> /*clip*/) const {
	const auto found = note_tails.find(slot.id);
	return found == note_tails.end() || found->second;
}

ModelFixture::ModelFixture() {
	host.tracks.bend_main = kDeviceBendRange;
	host.tracks.bend_mpe = kDeviceBendRangeMpe;
	song.output_structure.getOrCreate(kSynth.id);
	song.drum_structure.getOrCreate(kDrum.id);
	set_state(kSynth, instrument::new_synth_state());
	set_state(kDrum, instrument::new_synth_state());
	for (const project::ChainAddress chain : {kKitChain, kAudioChain}) {
		song.output_structure.getOrCreate(chain.id).effects = project::FxChain{};
	}
	for (const project::ChainAddress chain : {kKitChain, kAudioChain, kMasterChain}) {
		host.tracks.chains[FakeTrackFacts::chain_key(chain)] = effects::EffectStates{};
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
	host.sound.states[slot.id] = state;
	project::InstrumentSlot made{.plugin = "deluge.instrument",
	                             .state = plugin::StateHandle::of<instrument::DelugeSynth>(state),
	                             .tick_magnitude = 1};
	if (slot.kind == project::SlotAddress::Kind::OUTPUT) {
		song.output_structure.getOrCreate(slot.id).instrument = std::move(made);
	}
	else {
		song.drum_structure.getOrCreate(slot.id).instrument = std::move(made);
	}
}

std::optional<int32_t> FakeFacts::param_value(const project::ParamAddress& address) const {
	for (const auto& [at, value] : param_values) {
		if (at.owner == address.owner && at.space == address.space && at.id == address.id
		    && at.clip_id == address.clip_id) {
			return value;
		}
	}
	return std::nullopt;
}

} // namespace deluge::scenes::bind::test
