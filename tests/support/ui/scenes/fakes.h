#pragma once

/// @file
/// @brief Fakes of the scenes' ports: plain state a spec sets, and every call a sink takes, recorded in order.

#include "project/device_settings.h"
#include "project/midi_command.h"
#include "project/owner_plugin.h"
#include "project/param_address.h"
#include "serialization/value.h"
#include "ui/controls/sink.h"
#include "ui/scenes/bind/knob_focus.h"
#include "ui/scenes/bind/ports/param_info.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/views/waveform/peaks.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace deluge::ui::scenes::bind::test {

/// @brief AudioFacts a spec sets directly.
struct FakeAudioFacts final : AudioFacts {
	bool inputs{false};                                        ///< what `audio_inputs_present` answers
	int32_t voices{0};                                         ///< what `live_voice_count` answers
	bool line_in{false};                                       ///< what `line_in_plugged` answers
	int32_t threshold{0};                                      ///< what `song_threshold_recording` answers
	plugin::SyncType sidechain_type{plugin::SyncType::EVEN};   ///< what `reverb_sidechain_sync_type` answers
	bool busy{false};                                          ///< what `audio_busy` answers
	ForegroundRecording foreground{ForegroundRecording::NONE}; ///< what `foreground_recording` answers
	bool resample{false};                                      ///< what `resampling` answers
	std::optional<InputTake> take;                             ///< what `input_take` answers
	std::optional<std::string> take_path;                      ///< what `input_take_path` answers
	uint32_t sample_timer{0};                                  ///< what `audio_sample_timer` answers
	[[nodiscard]] bool audio_inputs_present() const override { return inputs; }
	[[nodiscard]] int32_t live_voice_count() const override { return voices; }
	[[nodiscard]] bool line_in_plugged() const override { return line_in; }
	[[nodiscard]] int32_t song_threshold_recording() const override { return threshold; }
	[[nodiscard]] plugin::SyncType reverb_sidechain_sync_type() const override { return sidechain_type; }
	[[nodiscard]] bool audio_busy() const override { return busy; }
	[[nodiscard]] ForegroundRecording foreground_recording() const override { return foreground; }
	[[nodiscard]] bool resampling() const override { return resample; }
	[[nodiscard]] std::optional<InputTake> input_take() const override { return take; }
	[[nodiscard]] std::optional<std::string> input_take_path() const override { return take_path; }
	[[nodiscard]] uint32_t audio_sample_timer() const override { return sample_timer; }
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
	PerformanceEditing performance{PerformanceEditing::Disabled}; ///< what `performance_editing_mode` answers
	int32_t battery_mv{0};                                        ///< what `battery_millivolts` answers
	std::string_view firmware{"2.0.0-test"};                      ///< what `firmware_version` answers
	[[nodiscard]] bool auditioning() const override { return auditioning_now; }
	[[nodiscard]] bool popup_showing() const override { return popup; }
	[[nodiscard]] PerformanceEditing performance_editing_mode() const override { return performance; }
	[[nodiscard]] int32_t battery_millivolts() const override { return battery_mv; }
	[[nodiscard]] std::string_view firmware_version() const override { return firmware; }
};

/// @brief SoundFacts a spec sets directly, keyed by slot id.
struct FakeSoundFacts final : SoundFacts {
	std::map<uint32_t, uint8_t> loaded;                              ///< what `loaded_sources` answers, by slot id
	std::map<uint32_t, instruments::classic::SoundState> states;     ///< what `sound_state` points at, by slot id
	mutable std::vector<project::SlotAddress> asked;                 ///< every slot `loaded_sources` was asked about
	std::map<uint32_t, bool> whole_folder;                           ///< what `kit_takes_whole_folder` answers
	std::map<std::pair<uint32_t, uint8_t>, uint16_t> default_ranges; ///< by {slot id, source}
	std::map<std::tuple<uint32_t, uint8_t, uint16_t>, uint64_t> identities; ///< by {slot id, source, range}
	std::optional<uint16_t> take;                                           ///< what `take_range` answers
	std::optional<DxCartridgeNames> cartridge;                              ///< what `dx_cartridge` answers
	std::vector<CableFact> cable_list;                                      ///< what `cables` answers
	[[nodiscard]] uint8_t loaded_sources(project::SlotAddress slot) const override;
	[[nodiscard]] const instruments::classic::SoundState* sound_state(project::SlotAddress slot) const override;
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
	std::optional<ClipNavigation> navigation;      ///< what `clip_navigation` answers
	[[nodiscard]] std::optional<AudioClipFact> audio_clip(project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_playing(project::ClipAddress clip) const override;
	[[nodiscard]] int32_t clip_live_pos(project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_arrangement_only(project::ClipAddress clip) const override;
	[[nodiscard]] bool clip_recording_linearly(project::ClipAddress clip) const override;
	[[nodiscard]] std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                             int32_t zoom) const override;
	[[nodiscard]] std::optional<IsomorphicKeyboard> isomorphic_keyboard() const override { return keyboard; }
	[[nodiscard]] std::optional<ClipNavigation> clip_navigation() const override { return navigation; }
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
class FakeControlFacts final : public ui::controls::Facts {
public:
	std::map<uint32_t, bool> note_tails; ///< by slot id; a sound absent here can hold tails
	/// The value each param plays, by its address; a param absent here the song does not hold.
	std::vector<std::pair<project::ParamAddress, int32_t>> param_values;
	bool alternative_delay{false}; ///< what `alt_delay` answers: the alternative delay knobs are on
	[[nodiscard]] bool allows_note_tails(project::SlotAddress slot,
	                                     std::optional<project::ClipAddress> clip) const override;
	[[nodiscard]] std::optional<int32_t> param_value(const project::ParamAddress& address) const override;
	[[nodiscard]] bool stutter_active() const override { return false; }
	[[nodiscard]] bool alt_delay() const override { return alternative_delay; }
	[[nodiscard]] std::span<const ui::controls::PlayingOutput> playing_outputs() const override { return {}; }
	[[nodiscard]] std::span<const ui::controls::KitRow> sound_rows(uint32_t /*clip_id*/) const override { return {}; }
	[[nodiscard]] ui::controls::TakeoverInputs takeover_inputs(bool cable_relative) const override {
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
			RANGES,              ///< write_ranges
			DEFAULT_RANGE,       ///< set_default_range
			INSERT_RANGE,        ///< insert_range
			DELETE_RANGE,        ///< delete_range
			ATTACH,              ///< attach_recording
			DX_PATCH,            ///< write_dx_patch
			DX_PROGRAM,          ///< load_dx_program
			TRANSPOSE,           ///< write_source_transpose
			VELOCITY_EVERYWHERE, ///< velocity_to_level_everywhere
		};
		Kind kind{Kind::RANGES};     ///< which request it was
		project::SlotAddress slot{}; ///< the sound it named (not TRANSPOSE's)
		uint8_t source{0};           ///< the source it named
		/// The range an ATTACH, DEFAULT_RANGE or TRANSPOSE names, or an INSERT_RANGE's position or a DELETE_RANGE's
		/// index; none for a source's own transpose.
		std::optional<uint16_t> range;
		std::vector<synth::SourceRange> ranges; ///< RANGES' ranges
		RangesCause cause{};                    ///< RANGES' cause
		/// INSERT_RANGE's new top note, DX_PROGRAM's program, TRANSPOSE's menu value.
		int64_t value{0};
		/// An INSERT_RANGE's new top for the range below the inserted one; a DELETE_RANGE's new top for the range
		/// below the deleted one. None where it keeps its top.
		std::optional<int16_t> neighbour_top;
		std::string text;                                           ///< ATTACH's path
		std::optional<instruments::classic::DxPatchState> dx_patch; ///< DX_PATCH's patch
		Addresses where{};                                          ///< TRANSPOSE's owner
		midi::DeviceId device{};                                    ///< VELOCITY_EVERYWHERE's device
	};
	std::vector<Call> calls; ///< every request taken, in order
	bool accept{true};       ///< what each request answers (set_default_range answers nothing)
	bool write_ranges(project::SlotAddress slot, uint8_t source, std::span<const synth::SourceRange> ranges,
	                  RangesCause cause) override;
	void set_default_range(project::SlotAddress slot, uint8_t source, uint16_t range) override;
	bool insert_range(project::SlotAddress slot, uint8_t source, uint16_t at, int16_t top_note,
	                  std::optional<int16_t> previous_top) override;
	bool delete_range(project::SlotAddress slot, uint8_t source, uint16_t index,
	                  std::optional<int16_t> lower_top) override;
	bool attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) override;
	bool write_dx_patch(project::SlotAddress slot, const instruments::classic::DxPatchState& patch) override;
	bool load_dx_program(project::SlotAddress slot, int32_t program) override;
	bool write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
	                            int32_t menu_value) override;
	bool velocity_to_level_everywhere(midi::DeviceId device) override;
};

struct FakeMidiDevices;

/// @brief LearnSink that records every learn, in order, and answers each with @ref accept. A global or follow learn is
///        also kept in @ref devices, when set, so the MIDI devices fake reads it back; one naming a device that fake
///        does not know is recorded and kept nowhere.
struct RecordingLearnSink final : LearnSink {
	/// @brief One recorded learn: its kind and its arguments.
	struct Call {
		/// @brief Which learn it was.
		enum class Kind : uint8_t {
			LEARN,        ///< learn
			LEARN_KNOB,   ///< learn_knob
			LEARN_GLOBAL, ///< learn_global
			LEARN_FOLLOW, ///< learn_follow
		};
		Kind kind{Kind::LEARN};                       ///< which learn it was
		std::optional<project::MappingTarget> target; ///< LEARN's target, or LEARN_KNOB's owner and param
		std::optional<project::MidiCommand> command;  ///< LEARN's command; none for an unlearn
		std::optional<LearnInput> input;              ///< LEARN's input note
		midi::DeviceId device{};                      ///< LEARN_KNOB's device; none for a mod knob
		int32_t which_knob{0};                        ///< LEARN_KNOB's mod knob or CC number
		int32_t midi_channel{0};                      ///< LEARN_KNOB's channel, 16 or more for a mod knob
		project::GlobalCommand global{};              ///< LEARN_GLOBAL's command
		project::FollowInput follow{};                ///< LEARN_FOLLOW's input
		std::optional<LearnedBinding> binding;        ///< LEARN_GLOBAL's or LEARN_FOLLOW's binding; none to unlearn
	};
	std::vector<Call> calls;           ///< every learn taken, in order
	bool accept{true};                 ///< what each learn answers
	FakeMidiDevices* devices{nullptr}; ///< where a global or follow learn is kept; none keeps nothing
	bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	           std::optional<LearnInput> input) override;
	bool learn_knob(const project::OwnerParamTarget& target, midi::DeviceId device, int32_t which_knob,
	                int32_t midi_channel) override;
	void learn_global(project::GlobalCommand command, std::optional<LearnedBinding> binding) override;
	void learn_follow(project::FollowInput input, std::optional<LearnedBinding> binding) override;
};

/// @brief MidiDevices a spec sets directly: DIN alone, connected, with default settings, until the spec says otherwise.
struct FakeMidiDevices final : MidiDevices {
	std::vector<midi::DeviceId> list{midi::kDin};                        ///< what `connected` answers, in order
	std::map<uint32_t, std::string> names{{midi::kDin.value, "DIN"}};    ///< what `name` answers, by handle
	std::map<uint32_t, MidiDeviceSettings> kept{{midi::kDin.value, {}}}; ///< what `settings` answers, by handle
	std::vector<std::pair<midi::DeviceId, MidiDeviceSettings>> writes;   ///< every write, in order, known or not
	std::map<project::GlobalCommand, LearnedBinding> globals;            ///< what `global_command` answers
	std::map<project::FollowInput, LearnedBinding> follows;              ///< what `follow_input` answers
	/// What `reference` answers, by handle.
	std::map<uint32_t, project::MidiDeviceRef> refs{{midi::kDin.value, {.port = project::MidiDeviceRef::Port::DIN}}};
	[[nodiscard]] std::span<const midi::DeviceId> connected() const override { return list; }
	[[nodiscard]] std::string_view name(midi::DeviceId device) const override;
	[[nodiscard]] std::optional<MidiDeviceSettings> settings(midi::DeviceId device) const override;
	[[nodiscard]] std::optional<project::MidiDeviceRef> reference(midi::DeviceId device) const override;
	/// @brief Records the write, and keeps @p settings when @p device is in @ref kept.
	/// @return Whether @p device is in @ref kept.
	bool write(midi::DeviceId device, const MidiDeviceSettings& settings) override;
	[[nodiscard]] std::optional<LearnedBinding> global_command(project::GlobalCommand command) const override;
	[[nodiscard]] std::optional<LearnedBinding> follow_input(project::FollowInput input) const override;
	/// @brief Know @p device as well: listed after the devices already connected, called @p device_name, kept with
	///        default settings, so a learn from it is kept, and referred to as a hosted device of that name.
	/// @param device      The device that connects.
	/// @param device_name What `name` answers for it.
	void connect(midi::DeviceId device, std::string_view device_name);
};

/// @brief DeviceActions that records every action, in order.
struct RecordingDeviceActions final : DeviceActions {
	/// @brief Which action it was.
	enum class Kind : uint8_t {
		SAVE_SETTINGS,   ///< save_settings
		DELETE_ALL_LOGS, ///< delete_all_logs
	};
	std::vector<Kind> calls;                       ///< every action taken, in order
	std::vector<std::string_view>* order{nullptr}; ///< when set, delete_all_logs appends "wipe" to it
	void save_settings() override { calls.push_back(Kind::SAVE_SETTINGS); }
	void delete_all_logs() override {
		calls.push_back(Kind::DELETE_ALL_LOGS);
		if (order != nullptr) {
			order->push_back("wipe");
		}
	}
};

/// @brief Transport a spec sets directly.
struct FakeTransport final : Transport {
	bool is_playing{false};                           ///< what `playing` answers
	bool clocks{false};                               ///< what `clocks_active` answers
	TransportRecording mode{TransportRecording::OFF}; ///< what `recording` answers
	bool metronome{false};                            ///< what `metronome_on` answers
	bool undo_ok{false};                              ///< what `undo_allowed` answers
	bool overdubs{false};                             ///< what `overdubs_pending` answers
	int32_t count_in{0};                              ///< what `count_in_visual` answers
	bool arrangement{false};                          ///< what `arrangement_mode` answers
	[[nodiscard]] bool playing() const override { return is_playing; }
	[[nodiscard]] bool clocks_active() const override { return clocks; }
	[[nodiscard]] TransportRecording recording() const override { return mode; }
	[[nodiscard]] bool metronome_on() const override { return metronome; }
	[[nodiscard]] bool undo_allowed() const override { return undo_ok; }
	[[nodiscard]] bool overdubs_pending() const override { return overdubs; }
	[[nodiscard]] int32_t count_in_visual() const override { return count_in; }
	[[nodiscard]] bool arrangement_mode() const override { return arrangement; }
};

/// @brief TransportActions that records every action, in order. When @ref transport is set, PLAY and the metronome
///        toggle flip its state, as the buttons do, so a read after the action sees it.
struct RecordingTransportActions final : TransportActions {
	/// @brief One recorded action.
	struct Call {
		/// @brief Which action it was.
		enum class Kind : uint8_t {
			PLAY,                              ///< play
			RECORD,                            ///< record
			TAP_TEMPO,                         ///< tap_tempo
			TOGGLE_METRONOME,                  ///< toggle_metronome
			CLEAR_TEMPO_AUTOMATION,            ///< clear_tempo_automation
			SHOW_SWING_INTERVAL,               ///< show_swing_interval
			SHOW_TEMPO,                        ///< show_tempo
			UNDO,                              ///< undo
			BEGIN_OUTPUT_RECORDING,            ///< begin_output_recording
			END_RECORDING_SOON,                ///< end_recording_soon
			STOP_OUTPUT_RECORDING_AT_LOOP_END, ///< stop_output_recording_at_loop_end
			FILL,                              ///< fill
			GRAB_TEMPO,                        ///< grab_tempo
		};
		Kind kind{Kind::PLAY};       ///< which action it was
		bool flag{false};            ///< TAP_TEMPO's normal, UNDO's redo, FILL's on
		project::ClipAddress clip{}; ///< GRAB_TEMPO's clip
		bool operator==(const Call&) const = default;
	};
	std::vector<Call> calls;           ///< every action, in order
	FakeTransport* transport{nullptr}; ///< the state PLAY and the metronome toggle flip; none flips nothing
	void play() override;
	void record() override { calls.push_back({.kind = Call::Kind::RECORD}); }
	void tap_tempo(bool normal) override { calls.push_back({.kind = Call::Kind::TAP_TEMPO, .flag = normal}); }
	void toggle_metronome() override;
	void clear_tempo_automation() override { calls.push_back({.kind = Call::Kind::CLEAR_TEMPO_AUTOMATION}); }
	void show_swing_interval() override { calls.push_back({.kind = Call::Kind::SHOW_SWING_INTERVAL}); }
	void show_tempo() override { calls.push_back({.kind = Call::Kind::SHOW_TEMPO}); }
	void undo(bool redo) override { calls.push_back({.kind = Call::Kind::UNDO, .flag = redo}); }
	void begin_output_recording() override { calls.push_back({.kind = Call::Kind::BEGIN_OUTPUT_RECORDING}); }
	void end_recording_soon() override { calls.push_back({.kind = Call::Kind::END_RECORDING_SOON}); }
	void stop_output_recording_at_loop_end() override {
		calls.push_back({.kind = Call::Kind::STOP_OUTPUT_RECORDING_AT_LOOP_END});
	}
	void fill(bool on) override { calls.push_back({.kind = Call::Kind::FILL, .flag = on}); }
	void grab_tempo(project::ClipAddress clip) override {
		calls.push_back({.kind = Call::Kind::GRAB_TEMPO, .clip = clip});
	}
};

/// @brief MidiFeedback a spec sets directly, and every send, in order.
struct RecordingMidiFeedback final : MidiFeedback {
	/// @brief One recorded send.
	struct Call {
		/// @brief Which send it was.
		enum class Kind : uint8_t {
			SEND,     ///< send
			SEND_ALL, ///< send_all
		};
		Kind kind{Kind::SEND};  ///< which send it was
		uint8_t cc{0};          ///< SEND's CC
		int32_t knob_pos{0};    ///< SEND's knob position
		bool automation{false}; ///< SEND_ALL's automation flag
		bool operator==(const Call&) const = default;
	};
	bool on{false};             ///< what `enabled` answers
	bool global_context{false}; ///< what `global_effectable_context` answers
	/// The CCs `cc_of` answers, by owner plugin and param; any other answers none.
	std::vector<std::tuple<project::OwnerPlugin, project::ParamTarget, uint8_t>> ccs;
	std::vector<Call> calls; ///< every send, in order
	[[nodiscard]] bool enabled() const override { return on; }
	[[nodiscard]] bool global_effectable_context() const override { return global_context; }
	[[nodiscard]] std::optional<uint8_t> cc_of(project::OwnerPlugin owner, project::ParamTarget target) const override;
	void send(uint8_t cc, int32_t knob_pos) override {
		calls.push_back({.kind = Call::Kind::SEND, .cc = cc, .knob_pos = knob_pos});
	}
	void send_all(bool automation) override {
		calls.push_back({.kind = Call::Kind::SEND_ALL, .automation = automation});
	}
};

/// @brief DeviceSettings a spec sets directly: every DEVICE row's value, seeded from its default, and every store, in
///        order. The port's own front checks each write, so a store is always one the rows allow.
struct FakeDeviceSettings final : DeviceSettings {
	FakeDeviceSettings();
	/// What `load` answers, by path, in file form; a spec may set a value the row cannot name.
	std::map<std::string, serialization::Value, std::less<>> values;
	std::set<std::string, std::less<>> accept_restart; ///< the paths a store answers `STORED_FOR_RESTART`
	std::set<std::string, std::less<>> refuse;         ///< the paths a store answers REFUSED, keeping nothing
	std::vector<std::pair<std::string, serialization::Value>> stores; ///< every store, in order, refused ones too

private:
	[[nodiscard]] serialization::Value load(const plugin::SettingInfo& row) const override;
	[[nodiscard]] DeviceWrite store(const plugin::SettingInfo& row, const serialization::Value& value) override;
};

/// @brief WriteSink that records every request, in order, and answers each with @ref accept.
struct RecordingWrites final : WriteSink {
	/// @brief A text, default-input-channel, threshold or sidechain-type request, as recorded.
	struct Gesture {
		/// @brief Which request it was.
		enum class Kind : uint8_t {
			TEXT,                  ///< write_text
			DEFAULT_INPUT_CHANNEL, ///< set_default_audio_input_channel
			THRESHOLD_RECORDING,   ///< set_song_threshold_recording
			SIDECHAIN_SYNC_TYPE,   ///< set_reverb_sidechain_sync_type
		};
		Kind kind{Kind::TEXT}; ///< which request it was
		Target target{};       ///< TEXT's row
		Addresses where{};     ///< TEXT's owner
		std::string text;      ///< TEXT's text
		int64_t value{0}; ///< DEFAULT_INPUT_CHANNEL's channel, THRESHOLD_RECORDING's mode, SIDECHAIN_SYNC_TYPE's type
	};
	std::vector<WriteRequest> requests;         ///< every write, in order
	std::vector<project::ParamAddress> deletes; ///< every delete_automation's param, in order
	std::vector<Gesture>
	    gestures;      ///< every text, default-input-channel, threshold and sidechain-type request, in order
	bool accept{true}; ///< what each request answers (set_default_audio_input_channel answers nothing)
	std::vector<std::string_view>* order{nullptr}; ///< when set, write_text appends "name" to it
	bool write(const WriteRequest& request) override;
	bool delete_automation(const project::ParamAddress& address) override;
	bool write_text(const Target& target, const Addresses& where, std::string_view text) override;
	void set_default_audio_input_channel(int32_t channel) override;
	void set_song_threshold_recording(int32_t mode) override;
	void set_reverb_sidechain_sync_type(plugin::SyncType type) override;
};

/// @brief controls::Sink that records every request, in order, and answers each with @ref accept; take_for answers
///        @ref take_answer.
struct RecordingControlsSink final : ui::controls::Sink {
	/// @brief A param write by address, as recorded.
	struct ParamWrite {
		project::ParamAddress address;                                       ///< the param written
		int32_t value{0};                                                    ///< its value
		int32_t pos{0};                                                      ///< the position written at
		int32_t length{0};                                                   ///< the length of the region written
		ui::controls::Resolution resolution{ui::controls::Resolution::KNOB}; ///< how its address resolves
	};
	/// @brief One of the knobs' other requests, as recorded.
	struct Gesture {
		/// @brief Which request it was.
		enum class Kind : uint8_t {
			KNOB_MODE,              ///< knob_mode
			CHAIN_VIEW,             ///< chain_view
			CONTROLS,               ///< write_controls
			STUTTER,                ///< stutter
			GRAB_REGION,            ///< grab_region
			EFFECT_WORD,            ///< write_effect
			MIDI_CC_SELECT,         ///< midi_cc_select
			DELETE_KNOB_AUTOMATION, ///< delete_knob_automation
			INSTRUMENT_EDITED,      ///< instrument_edited
		};
		Kind kind{Kind::KNOB_MODE}; ///< which request it was
		/// KNOB_MODE's mode, GRAB_REGION's position, MIDI_CC_SELECT's knob.
		int64_t value{0};
		std::optional<project::KnobModeOwner> knob_owner;         ///< KNOB_MODE's owner
		project::ChainAddress chain{};                            ///< CHAIN_VIEW's chain
		std::string key;                                          ///< CHAIN_VIEW's key, EFFECT_WORD's path
		std::string text;                                         ///< CHAIN_VIEW's word, EFFECT_WORD's word
		std::optional<project::ControlsAddress> controls_address; ///< CONTROLS' owner
		std::vector<project::ControllerMapping> controls;         ///< CONTROLS' mappings
		/// STUTTER's, GRAB_REGION's, MIDI_CC_SELECT's and INSTRUMENT_EDITED's focus.
		KnobFocus focus{};
		bool begin{false};                            ///< STUTTER's begin (true) or end; MIDI_CC_SELECT's on
		std::optional<EffectOwner> effect_owner;      ///< EFFECT_WORD's owner
		std::optional<project::ParamAddress> address; ///< DELETE_KNOB_AUTOMATION's param
	};
	std::vector<ParamWrite> param_writes;                                   ///< every write_param, in order
	std::vector<std::pair<project::ParamAddress, int32_t>> learned_follows; ///< every learned_written, in order
	std::vector<uint32_t> take_requests;                                    ///< every output take_for was asked about
	std::vector<project::ParamAddress> made;                                ///< every param make_param was asked for
	std::vector<std::pair<project::OwnerParamTarget, project::MidiCommand>> absolutes; ///< every knob_absolute
	std::vector<Gesture> gestures;       ///< every other request, in order
	std::optional<uint32_t> take_answer; ///< what take_for answers
	bool accept{true};                   ///< what each request that answers a bool answers
	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 ui::controls::Resolution resolution) override;
	void learned_written(const project::ParamAddress& address, int32_t knob_pos) override;
	std::optional<uint32_t> take_for(uint32_t output_id) override;
	bool make_param(const project::ParamAddress& address) override;
	void knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) override;
	bool knob_mode(project::KnobModeOwner owner, uint8_t mode) override;
	bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) override;
	bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) override;
	bool stutter(const KnobFocus& focus, bool begin) override;
	bool grab_region(const KnobFocus& focus, uint32_t pos) override;
	bool write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value& value) override;
	bool midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) override;
	bool delete_knob_automation(const project::ParamAddress& address) override;
	bool instrument_edited(const KnobFocus& focus) override;
};

/// @brief Waveforms a spec sets directly, and every peaks call, in order.
struct FakeWaveforms final : Waveforms {
	/// @brief One waveform the fake holds.
	struct Entry {
		WaveformAddress at;                                                   ///< where it is
		WaveformInfo info;                                                    ///< what `info` answers
		std::array<views::waveform::Peak, views::waveform::kColumns> peaks{}; ///< each column's peak
		std::bitset<views::waveform::kColumns> pending{}; ///< columns answered PENDING while reads remain
		int32_t pending_reads{0};                         ///< how many reads answer them PENDING
		std::optional<int64_t> position;                  ///< what `play_position` answers
		/// While `info.recording`, the frames the take has written: as on the host, a column that does not end by
		/// then is past the end, the one straddling it too. None reads as `info.length_samples`.
		std::optional<uint64_t> captured;
	};
	mutable std::vector<Entry> waveforms; ///< looked up by address; a read counts its entry's pending reads down
	/// @brief One recorded `peaks` call.
	struct Read {
		WaveformAddress at;         ///< the address
		views::waveform::Window in; ///< the window
		bool operator==(const Read&) const = default;
	};
	mutable std::vector<Read> reads; ///< every `peaks` call, in order
	[[nodiscard]] std::optional<WaveformInfo> info(const WaveformAddress& address) const override;
	[[nodiscard]] views::waveform::PeakRead peaks(const WaveformAddress& address,
	                                              views::waveform::Window at) const override;
	[[nodiscard]] std::optional<int64_t> play_position(const WaveformAddress& address) const override;
};

/// @brief TakeActions that records every call, in order, and plays its effect on a FakeAudioFacts' take when set.
struct RecordingTakeActions final : TakeActions {
	/// @brief Which call it was.
	enum class Kind : uint8_t {
		BEGIN,        ///< begin
		END_SOON,     ///< end_soon
		TAKE_LEVELS,  ///< take_levels
		TAKE_CLIPPED, ///< take_clipped
		FINISH,       ///< finish
		ORPHAN,       ///< orphan
	};
	std::vector<Kind> calls;                   ///< every call, in order
	TakeChannels channels{TakeChannels::MONO}; ///< the last begin's channels
	bool begins{true};                         ///< what `begin` answers
	InputLevels levels{};                      ///< what `take_levels` answers while a take is held
	bool clipped{false};                       ///< what the next `take_clipped` answers while a take is held; it clears
	FakeAudioFacts* audio{nullptr};            ///< begin holds a take there, finish and orphan drop it and its path
	std::vector<std::string_view>* order{nullptr}; ///< when set, begin appends "begin" to it
	[[nodiscard]] bool begin(TakeChannels c) override;
	void end_soon() override { calls.push_back(Kind::END_SOON); }
	[[nodiscard]] InputLevels take_levels() override {
		calls.push_back(Kind::TAKE_LEVELS);
		return held() ? levels : InputLevels{};
	}
	[[nodiscard]] bool take_clipped() override {
		calls.push_back(Kind::TAKE_CLIPPED);
		return held() && std::exchange(clipped, false);
	}
	void finish() override;
	void orphan() override;
	/// @brief Whether a take is held: always, with no audio fake to hold it on.
	[[nodiscard]] bool held() const { return audio == nullptr || audio->take.has_value(); }
};

/// @brief Indicators that record every call.
struct RecordingIndicators final : Indicators {
	int32_t tick_clears{0};                        ///< how many times the tick squares were cleared
	std::vector<std::string_view>* order{nullptr}; ///< when set, each clear appends "ticks" to it
	void clear_tick_squares() override {
		tick_clears++;
		if (order != nullptr) {
			order->push_back("ticks");
		}
	}
};

/// @brief ViewActions that record every audition end, in order.
struct RecordingViewActions final : ViewActions {
	std::vector<AuditionEnd> ends;                 ///< every end_audition's kind, in order
	std::vector<std::string_view>* order{nullptr}; ///< when set, each end appends "audition" to it
	void end_audition(AuditionEnd how) override {
		ends.push_back(how);
		if (order != nullptr) {
			order->push_back("audition");
		}
	}
};

/// @brief One of every fake, and the Ports that point at them: a spec's whole host.
struct FakeHost {
	FakeAudioFacts audio;                        ///< the audio port's fake
	FakeCardFacts card;                          ///< the card port's fake
	FakeUiFacts ui;                              ///< the UI port's fake
	FakeSoundFacts sound;                        ///< the sound port's fake
	FakeClipFacts clips;                         ///< the clip port's fake
	FakeTrackFacts tracks;                       ///< the track port's fake
	FakeControlFacts controls;                   ///< the knobs' port's fake
	RecordingClipWrites clip_writes;             ///< the clip port's recorder
	RecordingSoundWrites sound_writes;           ///< the sound port's recorder
	RecordingLearnSink learn;                    ///< the learn port's recorder
	FakeMidiDevices midi;                        ///< the MIDI devices port's fake
	RecordingWrites writes;                      ///< the menu writes' recorder
	RecordingControlsSink controls_sink;         ///< the knobs' writes' recorder
	FakeDeviceSettings device;                   ///< the device settings' fake
	RecordingDeviceActions device_actions;       ///< the device actions' recorder
	RecordingMidiFeedback midi_feedback;         ///< MIDI follow feedback's fake
	FakeTransport transport;                     ///< the transport's fake
	RecordingTransportActions transport_actions; ///< the transport actions' recorder
	FakeWaveforms waveforms;                     ///< the waveforms' fake
	RecordingTakeActions take_actions;           ///< the take actions' recorder
	RecordingIndicators indicators;              ///< the indicators' recorder
	RecordingViewActions view_actions;           ///< the view actions' recorder
	/// The wipe, the drum's name, the tick clear, the take's begin and the audition end, in the order they came.
	std::vector<std::string_view> order;
	/// @brief Ports pointing at this host's fakes; valid while it lives. The learn recorder keeps its global and
	///        follow learns in the MIDI devices fake, the transport actions flip the transport fake, the take actions
	///        hold their take on the audio fake, and the entry's recorders append to @ref order.
	[[nodiscard]] Ports ports() {
		learn.devices = &midi;
		transport_actions.transport = &transport;
		take_actions.audio = &audio;
		device_actions.order = &order;
		writes.order = &order;
		indicators.order = &order;
		take_actions.order = &order;
		view_actions.order = &order;
		return {.audio = &audio,
		        .card = &card,
		        .ui = &ui,
		        .sound = &sound,
		        .clips = &clips,
		        .tracks = &tracks,
		        .controls = &controls,
		        .clip_writes = &clip_writes,
		        .sound_writes = &sound_writes,
		        .learn = &learn,
		        .midi = &midi,
		        .writes = &writes,
		        .controls_sink = &controls_sink,
		        .device = &device,
		        .device_actions = &device_actions,
		        .midi_feedback = &midi_feedback,
		        .transport = &transport,
		        .transport_actions = &transport_actions,
		        .waveforms = &waveforms,
		        .take_actions = &take_actions,
		        .indicators = &indicators,
		        .view_actions = &view_actions};
	}
};

} // namespace deluge::ui::scenes::bind::test
