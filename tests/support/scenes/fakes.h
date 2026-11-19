#pragma once

/// @file
/// @brief Fakes of the scenes' ports: plain state a spec sets, and every call a sink takes, recorded in order.

#include "scenes/bind/ports/param_info.h"
#include "scenes/bind/ports/ports.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

/// @brief The knobs' reads, as a spec sets them: each note-tail answer and the params' values, by their addresses.
class FakeControlFacts final : public controls::Facts {
public:
	std::map<uint32_t, bool> note_tails; ///< by slot id; a sound absent here can hold tails
	/// The value each param plays, by its address; a param absent here the song does not hold.
	std::vector<std::pair<project::ParamAddress, int32_t>> param_values;
	[[nodiscard]] bool allows_note_tails(project::SlotAddress slot,
	                                     std::optional<project::ClipAddress> clip) const override;
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
	[[nodiscard]] const plugin::ParamInfo* param_info(plugin::PluginId plugin,
	                                                  const project::ParamTarget& target) const override {
		return deluge_param_info(plugin, target);
	}
};

/// @brief ClipWrites that records every request, in order, and answers each with @ref accept.
struct RecordingClipWrites final : ClipWrites {
	/// @brief One recorded request: its kind and its arguments.
	struct Call {
		/// @brief Which request it was.
		enum class Kind : uint8_t { RECOLOUR, CLEAR, MARKER, RESIZE, UNDO_SCROLL };
		Kind kind{Kind::RECOLOUR};   ///< which request it was
		project::ClipAddress clip{}; ///< the clip it named
		int64_t value{0};            ///< RECOLOUR's offset, MARKER's value, RESIZE's new length, UNDO_SCROLL's scroll
		int32_t old_length{0};       ///< RESIZE's length the gesture started from
		ResizeKind resize{ResizeKind::END}; ///< RESIZE's kind
		std::string path;                   ///< MARKER's row
		bool clear_automation{false};       ///< CLEAR's
		bool clear_sequence_and_mpe{false}; ///< CLEAR's
		bool operator==(const Call&) const = default;
	};
	std::vector<Call> calls; ///< every request taken, in order
	bool accept{true};       ///< what each request answers
	bool recolour_clip(project::ClipAddress clip, int32_t offset) override;
	bool clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) override;
	bool move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) override;
	bool resize_audio_clip(project::ClipAddress clip, ResizeKind kind, int32_t new_length, int32_t old_length) override;
	void set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) override;
};

/// @brief SoundWrites that records every request, in order, and answers each with @ref accept.
struct RecordingSoundWrites final : SoundWrites {
	/// @brief One recorded request: its kind and its arguments.
	struct Call {
		/// @brief Which request it was.
		enum class Kind : uint8_t {
			RANGES,        ///< write_ranges
			DEFAULT_RANGE, ///< set_default_range
			INSERT_RANGE,  ///< insert_range
			DELETE_RANGE,  ///< delete_range
			ATTACH,        ///< attach_recording
			DX_PATCH,      ///< write_dx_patch
			DX_PROGRAM,    ///< load_dx_program
			TRANSPOSE,     ///< write_source_transpose
		};
		Kind kind{Kind::RANGES};     ///< which request it was
		project::SlotAddress slot{}; ///< the sound it named (not TRANSPOSE's)
		uint8_t source{0};           ///< the source it named
		/// The range an ATTACH, DEFAULT_RANGE or TRANSPOSE names, or an INSERT_RANGE's position or a DELETE_RANGE's
		/// index; none for a source's own transpose.
		std::optional<uint16_t> range;
		std::vector<instrument::RangeState> ranges; ///< RANGES' ranges
		RangesCause cause{};                        ///< RANGES' cause
		/// INSERT_RANGE's new top note, DX_PROGRAM's program, TRANSPOSE's menu value.
		int64_t value{0};
		/// An INSERT_RANGE's new top for the range below the inserted one; a DELETE_RANGE's new top for the range
		/// below the deleted one. None where it keeps its top.
		std::optional<int16_t> neighbour_top;
		std::string text;                                 ///< ATTACH's path
		std::optional<instrument::DxPatchState> dx_patch; ///< DX_PATCH's patch
		Addresses where{};                                ///< TRANSPOSE's owner
	};
	std::vector<Call> calls; ///< every request taken, in order
	bool accept{true};       ///< what each request that answers, answers
	bool write_ranges(project::SlotAddress slot, uint8_t source, std::span<const instrument::RangeState> ranges,
	                  RangesCause cause) override;
	void set_default_range(project::SlotAddress slot, uint8_t source, uint16_t range) override;
	bool insert_range(project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
	                  std::optional<int16_t> previous_top) override;
	bool delete_range(project::SlotAddress slot, uint8_t source, uint16_t index,
	                  std::optional<int16_t> lower_top) override;
	bool attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) override;
	bool write_dx_patch(project::SlotAddress slot, uint8_t source, const instrument::DxPatchState& patch) override;
	bool load_dx_program(project::SlotAddress slot, uint8_t source, int32_t program) override;
	bool write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
	                            int32_t menu_value) override;
};

/// @brief One of every fake, and the Ports that point at them: a spec's whole host.
struct FakeHost {
	FakeAudioFacts audio;              ///< the audio port's fake
	FakeCardFacts card;                ///< the card port's fake
	FakeUiFacts ui;                    ///< the UI port's fake
	FakeSoundFacts sound;              ///< the sound port's fake
	FakeClipFacts clips;               ///< the clip port's fake
	FakeTrackFacts tracks;             ///< the track port's fake
	FakeControlFacts controls;         ///< the knobs' port's fake
	RecordingClipWrites clip_writes;   ///< the clip port's recorder
	RecordingSoundWrites sound_writes; ///< the sound port's recorder
	/// @brief Ports pointing at this host's fakes; valid while it lives.
	[[nodiscard]] Ports ports() {
		return {.audio = &audio,
		        .card = &card,
		        .ui = &ui,
		        .sound = &sound,
		        .clips = &clips,
		        .tracks = &tracks,
		        .controls = &controls,
		        .clip_writes = &clip_writes,
		        .sound_writes = &sound_writes};
	}
};

} // namespace deluge::scenes::bind::test
