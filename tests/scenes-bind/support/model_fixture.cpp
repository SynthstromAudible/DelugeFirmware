#include "support/model_fixture.h"

#include "engines/deluge/codec.h"

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
}

void ModelFixture::set_state(project::SlotAddress slot, const instrument::SoundState& state) {
	facts.states[slot.id] = state;
	project::InstrumentSlot made{.engine = "deluge.instrument",
	                             .state = instrument::encode(state, engine::CodecContext{.input_tick_magnitude = 1}),
	                             .tick_magnitude = 1};
	if (slot.kind == project::SlotAddress::Kind::OUTPUT) {
		song.output_structure.getOrCreate(slot.id).instrument = std::move(made);
	}
	else {
		song.drum_structure.getOrCreate(slot.id).instrument = std::move(made);
	}
}

} // namespace deluge::scenes::bind::test
