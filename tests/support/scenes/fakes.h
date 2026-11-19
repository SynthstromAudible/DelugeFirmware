#pragma once

/// @file
/// @brief Fakes of the scenes' ports: plain state a spec sets, and every call a sink takes, recorded in order.

#include "scenes/bind/ports/ports.h"

#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace deluge::scenes::bind::test {

/// @brief AudioFacts a spec sets directly.
struct FakeAudioFacts final : AudioFacts {
	bool inputs{false}; ///< what `audio_inputs_present` answers
	int32_t voices{0};  ///< what `live_voice_count` answers
	[[nodiscard]] bool audio_inputs_present() const override { return inputs; }
	[[nodiscard]] int32_t live_voice_count() const override { return voices; }
};

/// @brief CardFacts a spec sets directly.
struct FakeCardFacts final : CardFacts {
	bool sample_loading{false}; ///< what `sample_load_in_flight` answers
	bool replacing{false};      ///< what `output_may_be_replaced` answers
	[[nodiscard]] bool sample_load_in_flight() const override { return sample_loading; }
	[[nodiscard]] bool output_may_be_replaced() const override { return replacing; }
};

/// @brief UiFacts a spec sets directly.
struct FakeUiFacts final : UiFacts {
	bool auditioning_now{false};                                  ///< what `auditioning` answers
	bool popup{false};                                            ///< what `popup_showing` answers
	FlashCursor cursor{FlashCursor::Slow};                        ///< what `flash_cursor` answers
	PerformanceEditing performance{PerformanceEditing::Disabled}; ///< what `performance_editing_mode` answers
	bool dx7_engine{false};                                       ///< what `dx7_engine_enabled` answers
	[[nodiscard]] bool auditioning() const override { return auditioning_now; }
	[[nodiscard]] bool popup_showing() const override { return popup; }
	[[nodiscard]] FlashCursor flash_cursor() const override { return cursor; }
	[[nodiscard]] PerformanceEditing performance_editing_mode() const override { return performance; }
	[[nodiscard]] bool dx7_engine_enabled() const override { return dx7_engine; }
};

/// @brief SoundFacts a spec sets directly, keyed by slot id.
struct FakeSoundFacts final : SoundFacts {
	std::map<uint32_t, uint8_t> loaded;                              ///< what `loaded_sources` answers, by slot id
	std::map<uint32_t, instrument::SoundState> states;               ///< what `sound_state` points at, by slot id
	mutable std::vector<project::SlotAddress> asked;                 ///< every slot `loaded_sources` was asked about
	std::map<uint32_t, bool> whole_folder;                           ///< what `kit_takes_whole_folder` answers
	std::map<std::pair<uint32_t, uint8_t>, uint16_t> default_ranges; ///< by {slot id, source}
	std::map<std::tuple<uint32_t, uint8_t, uint16_t>, uint64_t> identities; ///< by {slot id, source, range}
	std::optional<uint16_t> take;                                           ///< what `take_range` answers
	std::optional<DxCartridgeNames> cartridge;                              ///< what `dx_cartridge` answers
	std::vector<CableFact> cable_list;                                      ///< what `cables` answers
	[[nodiscard]] uint8_t loaded_sources(project::SlotAddress slot) const override;
	[[nodiscard]] const instrument::SoundState* sound_state(project::SlotAddress slot) const override;
	[[nodiscard]] bool kit_takes_whole_folder(project::SlotAddress slot) const override;
	[[nodiscard]] std::optional<uint16_t> default_range(project::SlotAddress slot, uint8_t source) const override;
	[[nodiscard]] std::optional<uint64_t> range_identity(project::SlotAddress slot, uint8_t source,
	                                                     uint16_t range) const override;
	[[nodiscard]] std::optional<uint16_t> take_range(const TakeTarget& target) const override;
	[[nodiscard]] std::optional<DxCartridgeNames> dx_cartridge() const override { return cartridge; }
	[[nodiscard]] CableList cables(const project::ParamAddress& owner) const override;
};

/// @brief ClipFacts a spec sets directly, keyed by clip id.
struct FakeClipFacts final : ClipFacts {
	std::map<uint32_t, AudioClipFact> audio_clips; ///< what `audio_clip` answers, by clip id
	std::map<uint32_t, int32_t> playing_at;        ///< a clip here plays, at this position
	std::map<uint32_t, bool> arrangement_only;     ///< what `clip_arrangement_only` answers, by clip id
	std::map<uint32_t, bool> recording_linearly;   ///< what `clip_recording_linearly` answers, by clip id
	std::pair<int64_t, int64_t> samples{0, 0};     ///< what `audio_clip_samples` answers for a clip in `audio_clips`
	std::optional<IsomorphicKeyboard> keyboard;    ///< what `isomorphic_keyboard` answers
	[[nodiscard]] std::optional<AudioClipFact> audio_clip(project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_playing(project::ClipAddress clip) const override;
	[[nodiscard]] int32_t clip_live_pos(project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_arrangement_only(project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_recording_linearly(project::ClipAddress clip) const override;
	[[nodiscard]] std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                             int32_t zoom) const override;
	[[nodiscard]] std::optional<IsomorphicKeyboard> isomorphic_keyboard() const override { return keyboard; }
};

/// @brief TrackFacts a spec sets directly.
struct FakeTrackFacts final : TrackFacts {
	std::vector<TrackFact> track_list;                ///< what `tracks` answers
	int32_t bend_main{7};                             ///< the main bend range `played_bend_range` answers
	int32_t bend_mpe{30};                             ///< the MPE bend range `played_bend_range` answers
	uint8_t preset{0};                                ///< what `arp_preset` answers for every owner
	std::map<uint32_t, effects::EffectStates> chains; ///< by chain key: output id, or UINT32_MAX for the master's
	/// @brief A chain's key in `chains`.
	/// @param chain The chain's address.
	/// @return Its output id, or UINT32_MAX for the song's (the master's).
	[[nodiscard]] static uint32_t chain_key(project::ChainAddress chain);
	[[nodiscard]] std::vector<TrackFact> tracks() const override { return track_list; }
	[[nodiscard]] int32_t played_bend_range(const project::SettingAddress& owner, int32_t which) const override;
	[[nodiscard]] uint8_t arp_preset(const project::SettingAddress& /*address*/) const override { return preset; }
	[[nodiscard]] const effects::EffectStates* chain_effects(project::ChainAddress chain) const override;
};

/// @brief One of every fake, and the Ports that point at them: a spec's whole host.
struct FakeHost {
	FakeAudioFacts audio;  ///< the audio port's fake
	FakeCardFacts card;    ///< the card port's fake
	FakeUiFacts ui;        ///< the UI port's fake
	FakeSoundFacts sound;  ///< the sound port's fake
	FakeClipFacts clips;   ///< the clip port's fake
	FakeTrackFacts tracks; ///< the track port's fake
	/// @brief Ports pointing at this host's fakes; valid while it lives.
	[[nodiscard]] Ports ports() const {
		return {.audio = &audio, .card = &card, .ui = &ui, .sound = &sound, .clips = &clips, .tracks = &tracks};
	}
};

} // namespace deluge::scenes::bind::test
