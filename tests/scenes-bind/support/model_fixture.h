#pragma once

/// @file
/// @brief A project::Song with a synth track and a sound kit row, each with an instrument slot, and fake runtime facts
/// holding each sound's typed state.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/song.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/state.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <map>
#include <vector>

namespace deluge::scenes::bind::test {

/// The synth track's slot and the kit row's.
inline constexpr project::SlotAddress kSynth{project::SlotAddress::Kind::OUTPUT, 4};
inline constexpr project::SlotAddress kDrum{project::SlotAddress::Kind::DRUM, 9};
/// A kit's chain, an audio track's chain and the master's.
inline constexpr project::ChainAddress kKitChain{project::ChainAddress::Kind::OUTPUT, 20};
inline constexpr project::ChainAddress kAudioChain{project::ChainAddress::Kind::OUTPUT, 21};
inline constexpr project::ChainAddress kMasterChain{project::ChainAddress::Kind::SONG, 0};

/// Runtime facts a spec sets directly: plugged inputs, loaded sources and each sound's typed state.
class FakeFacts final : public RuntimeFacts {
public:
	bool inputs{false};
	std::map<uint32_t, uint8_t> loaded;                ///< by slot id
	std::map<uint32_t, instrument::SoundState> states; ///< by slot id
	mutable std::vector<project::SlotAddress> asked{}; ///< every slot loaded_sources was asked about
	std::map<uint32_t, effects::EffectStates> chains;  ///< by chain_key

	/// A chain's key in chains: its output id, or UINT32_MAX for the master's.
	[[nodiscard]] static uint32_t chain_key(project::ChainAddress chain);

	[[nodiscard]] bool audio_inputs_present() const override { return inputs; }
	[[nodiscard]] uint8_t loaded_sources(project::SlotAddress slot) const override;
	[[nodiscard]] const instrument::SoundState* sound_state(project::SlotAddress slot) const override;
	[[nodiscard]] const effects::EffectStates* chain_effects(project::ChainAddress chain) const override;
};

/// A song whose synth track (kSynth) and kit row (kDrum) hold the slot encode() writes for @p facts' states, which
/// start as a new synth's; and whose kit (kKitChain) and audio track (kAudioChain) have a chain, as the master does,
/// each with default effects in @p facts.
struct ModelFixture {
	project::Song song;
	FakeFacts facts;
	ModelFixture();
	/// Set @p slot's state and re-encode its slot from it, as a coherent song holds them.
	void set_state(project::SlotAddress slot, const instrument::SoundState& state);
};

} // namespace deluge::scenes::bind::test
