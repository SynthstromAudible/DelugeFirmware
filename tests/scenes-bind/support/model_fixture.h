#pragma once

/// @file
/// @brief A project::Song with a synth track and a sound kit row, each with an instrument slot, and fake runtime facts
/// holding each sound's typed state.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/state.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <map>
#include <optional>
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
/// The device's default bend ranges FakeFacts gives (main, MPE).
inline constexpr int32_t kDeviceBendRange = 7;
inline constexpr int32_t kDeviceBendRangeMpe = 30;

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
	std::map<uint32_t, bool> note_tails; ///< by slot id; a sound absent here can hold tails
	uint8_t preset{0};                   ///< the arp preset every owner's engine holds
	/// As a set made for the owner plays: a kit row's finger range starts at the main one.
	[[nodiscard]] int32_t played_bend_range(const project::SettingAddress& owner, int32_t which) const override {
		return which == 0 || owner.group == project::SettingGroupId::NOTE_ROW ? kDeviceBendRange : kDeviceBendRangeMpe;
	}
	[[nodiscard]] bool allows_note_tails(project::SlotAddress slot) const override;
	[[nodiscard]] uint8_t arp_preset(const project::SettingAddress& /*address*/) const override { return preset; }
	/// The value each param plays, by its address; a param absent here the song does not hold.
	std::vector<std::pair<project::ParamAddress, int32_t>> param_values;
	[[nodiscard]] std::optional<int32_t> param_value(const project::ParamAddress& address) const override;
	std::vector<CableFact> cable_list;
	[[nodiscard]] CableList cables(const project::ParamAddress& owner) const override;
	std::vector<TrackFact> track_list;
	[[nodiscard]] std::vector<TrackFact> tracks() const override { return track_list; }
	[[nodiscard]] std::optional<AudioClipFact> audio_clip(project::ClipAddress /*clip*/) const override {
		return std::nullopt;
	}
	[[nodiscard]] bool clip_playing(project::ClipAddress /*clip*/) const override { return false; }
	[[nodiscard]] int32_t clip_live_pos(project::ClipAddress /*clip*/) const override { return 0; }
	[[nodiscard]] bool clip_recording_linearly(project::ClipAddress /*clip*/) const override { return false; }
	[[nodiscard]] std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress /*clip*/, int32_t /*scroll*/,
	                                                             int32_t /*zoom*/) const override {
		return {0, 0};
	}
};

/// A song whose synth track (kSynth) and kit row (kDrum) hold the slot encode() writes for @p facts' states, which
/// start as a new synth's; whose kit (kKitChain) and audio track (kAudioChain) have a chain, as the master does,
/// each with default effects in @p facts; and which holds the model groups' owners above, each clip and lane with an
/// arp record.
struct ModelFixture {
	project::Song song;
	FakeFacts facts;
	ModelFixture();
	/// Set @p slot's state and re-encode its slot from it, as a coherent song holds them.
	void set_state(project::SlotAddress slot, const instrument::SoundState& state);
};

} // namespace deluge::scenes::bind::test
