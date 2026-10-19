#pragma once

/// @file
/// @brief A project::Song with a synth track and a sound kit row, each with an instrument slot, and fake runtime facts
/// holding each sound's typed state.

#include "deluge-project/instrument_slot.h"
#include "deluge-project/song.h"
#include "engines/deluge/sound_state.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <map>
#include <vector>

namespace deluge::scenes::bind::test {

/// The synth track's slot and the kit row's.
inline constexpr project::SlotAddress kSynth{project::SlotAddress::Kind::OUTPUT, 4};
inline constexpr project::SlotAddress kDrum{project::SlotAddress::Kind::DRUM, 9};

/// Runtime facts a spec sets directly: plugged inputs, loaded sources and each sound's typed state.
class FakeFacts final : public RuntimeFacts {
public:
	bool inputs{false};
	std::map<uint32_t, uint8_t> loaded;                ///< by slot id
	std::map<uint32_t, instrument::SoundState> states; ///< by slot id
	mutable std::vector<project::SlotAddress> asked{}; ///< every slot loaded_sources was asked about

	[[nodiscard]] bool audio_inputs_present() const override { return inputs; }
	[[nodiscard]] uint8_t loaded_sources(project::SlotAddress slot) const override;
	[[nodiscard]] const instrument::SoundState* sound_state(project::SlotAddress slot) const override;
};

/// A song whose synth track (kSynth) and kit row (kDrum) hold the slot encode() writes for @p facts' states, which
/// start as a new synth's.
struct ModelFixture {
	project::Song song;
	FakeFacts facts;
	ModelFixture();
	/// Set @p slot's state and re-encode its slot from it, as a coherent song holds them.
	void set_state(project::SlotAddress slot, const instrument::SoundState& state);
};

} // namespace deluge::scenes::bind::test
