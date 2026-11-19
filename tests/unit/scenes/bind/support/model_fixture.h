#pragma once

/// @file
/// @brief A project::Song with a synth track and a sound kit row, each with an instrument slot, and fake ports
/// holding each sound's typed state.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "effects/state.h"
#include "fakes.h"
#include "instruments/deluge/sound_state.h"
#include "scenes/bind/ports/ports.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace deluge::scenes::bind::test {

/// The synth track's slot and the kit row's.
inline constexpr project::SlotAddress kSynth{project::SlotAddress::Kind::OUTPUT, 4};
inline constexpr project::SlotAddress kDrum{project::SlotAddress::Kind::DRUM, 9};
/// A kit's chain, an audio track's chain and the master's.
inline constexpr project::ChainAddress kKitChain{project::ChainAddress::Kind::OUTPUT, 20};
inline constexpr project::ChainAddress kAudioChain{project::ChainAddress::Kind::OUTPUT, 21};
inline constexpr project::ChainAddress kMasterChain{project::ChainAddress::Kind::SONG, 0};

/// The model groups' owners: the synth track's clip; a MIDI track and its clip; a CV track on its second channel; an
/// audio track and its clip; a kit, its clip, and its sound (kDrum), MIDI and gate rows; the kit clip's sound row.
inline constexpr project::ClipAddress kSynthClip{35};
inline constexpr project::TrackAddress kMidiTrack{30};
inline constexpr project::ClipAddress kMidiClip{31};
inline constexpr project::TrackAddress kCvTrack{32};
inline constexpr project::TrackAddress kAudioTrack{33};
inline constexpr project::ClipAddress kAudioClip{34};
inline constexpr uint32_t kKit{40};
inline constexpr project::ClipAddress kKitClip{41};
inline constexpr project::LaneAddress kSoundLane{kKit, kDrum.id};
inline constexpr project::LaneAddress kMidiLane{kKit, 42};
inline constexpr project::LaneAddress kGateLane{kKit, 43};
inline constexpr project::NoteRowAddress kSoundRow{
    .clip_id = kKitClip.clip_id, .is_kit = true, .row_key = static_cast<int32_t>(kDrum.id)};
/// The device's default bend ranges the fixture's track facts give (main, MPE).
inline constexpr int32_t kDeviceBendRange = 7;
inline constexpr int32_t kDeviceBendRangeMpe = 30;

/// The knobs' reads, as a spec sets them: each note-tail answer and the params' values, by their addresses.
class FakeFacts final : public RuntimeFacts {
public:
	std::map<uint32_t, bool> note_tails; ///< by slot id; a sound absent here can hold tails
	[[nodiscard]] bool allows_note_tails(project::SlotAddress slot,
	                                     std::optional<project::ClipAddress> clip) const override;
	/// The value each param plays, by its address; a param absent here the song does not hold.
	std::vector<std::pair<project::ParamAddress, int32_t>> param_values;
	[[nodiscard]] std::optional<int32_t> param_value(const project::ParamAddress& address) const override;
	[[nodiscard]] bool stutter_active() const override { return false; }
	[[nodiscard]] bool alt_delay() const override { return false; }
	[[nodiscard]] std::span<const controls::PlayingOutput> playing_outputs() const override { return {}; }
	[[nodiscard]] std::span<const controls::KitRow> sound_rows(uint32_t /*clip_id*/) const override { return {}; }
	[[nodiscard]] controls::TakeoverInputs takeover_inputs(bool cable_relative) const override {
		return {.cable_relative = cable_relative};
	}
	[[nodiscard]] std::optional<std::pair<int32_t, int32_t>>
	step_region(std::optional<uint32_t> /*clip_id*/) const override {
		return std::nullopt;
	}
	[[nodiscard]] bool param_automated(const project::ParamAddress& /*address*/) const override { return false; }
	[[nodiscard]] std::optional<int32_t> clip_param_value(const project::ParamAddress& /*address*/,
	                                                      std::optional<uint32_t> /*pos*/) const override {
		return std::nullopt;
	}
	[[nodiscard]] std::optional<int32_t> param_value_at(const project::ParamAddress& /*address*/,
	                                                    uint32_t /*pos*/) const override {
		return std::nullopt;
	}
};

/// A song whose synth track (kSynth) and kit row (kDrum) hold the slot encode() writes for the host's sound states,
/// which start as a new synth's; whose kit (kKitChain) and audio track (kAudioChain) have a chain, as the master does,
/// each with default effects in the host's track facts; and which holds the model groups' owners above, each clip and
/// lane with an arp record.
struct ModelFixture {
	project::Song song;
	FakeFacts facts; ///< the knobs' reads, which `ports()` serves as `controls`
	FakeHost host; ///< the fakes of the other ports: each sound's typed state, the plugged inputs, each chain's effects
	ModelFixture();
	/// The host's ports, with `controls` the fixture's `facts`; valid while this fixture lives.
	[[nodiscard]] Ports ports() const {
		Ports held = host.ports();
		held.controls = &facts;
		return held;
	}
	/// Set @p slot's state and re-encode its slot from it, as a coherent song holds them.
	void set_state(project::SlotAddress slot, const instrument::SoundState& state);
};

} // namespace deluge::scenes::bind::test
