#pragma once

/// @file
/// @brief Cases every implementation of a port must pass, the fake and the host's alike. Each takes the port under
///        test and returns whether it holds; a spec states the case and expects true.

#include "base/divide.h"
#include "plugin/types/display_mapping.h"
#include "project/device_settings.h"
#include "project/midi_command.h"
#include "project/owner_plugin.h"
#include "project/param_address.h"
#include "project/transport.h"
#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/views/waveform/peaks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace deluge::ui::scenes::bind::contract {

/// @brief A port answers the same thing twice when nothing changed between the calls.
[[nodiscard]] inline bool ui_facts_are_stable(const UiFacts& ui) {
	return ui.auditioning() == ui.auditioning() && ui.popup_showing() == ui.popup_showing()
	       && ui.performance_editing_mode() == ui.performance_editing_mode();
}

/// @brief The card's facts are stable across two calls. No case that the host and a fake both answer the same way
///        can fail on a correct host's idle state, so a falsifiable CardFacts case is the fake's alone (the unit spec
///        sets it loading and expects true).
[[nodiscard]] inline bool card_facts_are_stable(const CardFacts& card) {
	return card.sample_load_in_flight() == card.sample_load_in_flight()
	       && card.output_may_be_replaced() == card.output_may_be_replaced()
	       && card.last_sample_path() == card.last_sample_path();
}

/// @brief The voice count is never negative.
[[nodiscard]] inline bool voice_count_is_a_count(const AudioFacts& audio) {
	return audio.live_voice_count() >= 0;
}

/// @brief The threshold-recording mode is one of the menu's four choices, and the sidechain's sync type one of the
///        three it lists. (The line-input fact has no case a fake and the host share; each spec checks it against
///        its own source.)
[[nodiscard]] inline bool audio_runtime_facts_are_choices(const AudioFacts& audio) {
	const int32_t threshold = audio.song_threshold_recording();
	const plugin::SyncType type = audio.reverb_sidechain_sync_type();
	return threshold >= 0 && threshold <= 3
	       && std::ranges::find(plugin::kSyncTypeChoices, type) != plugin::kSyncTypeChoices.end();
}

/// @brief Host-only: a recording fake cannot read back. The caller must have a song, since the threshold lives on it.
///        Each runtime write is visible through the audio fact that reads it back. Meaningful on the host, whose
///        writes land in the engine; the caller restores what it changed.
[[nodiscard]] inline bool runtime_writes_are_read_back(WriteSink& writes, const AudioFacts& audio) {
	const int32_t threshold = audio.song_threshold_recording();
	const plugin::SyncType type = audio.reverb_sidechain_sync_type();
	const int32_t other_threshold = threshold == 2 ? 1 : 2;
	const plugin::SyncType other_type =
	    type == plugin::SyncType::TRIPLET ? plugin::SyncType::DOTTED : plugin::SyncType::TRIPLET;
	writes.set_song_threshold_recording(other_threshold);
	writes.set_reverb_sidechain_sync_type(other_type);
	const bool read_back =
	    audio.song_threshold_recording() == other_threshold && audio.reverb_sidechain_sync_type() == other_type;
	writes.set_song_threshold_recording(threshold);
	writes.set_reverb_sidechain_sync_type(type);
	return read_back && audio.song_threshold_recording() == threshold && audio.reverb_sidechain_sync_type() == type;
}

/// @brief The firmware version is a non-empty view over storage that outlives the call: the same bytes answer twice.
[[nodiscard]] inline bool firmware_version_is_a_stable_view(const UiFacts& ui) {
	const std::string_view first = ui.firmware_version();
	const std::string_view second = ui.firmware_version();
	return !first.empty() && first.data() == second.data() && first == second;
}

/// @brief A slot no song holds has no state, no loaded source, no cables and no ranges.
[[nodiscard]] inline bool unknown_slot_is_empty(const SoundFacts& sound, project::SlotAddress nowhere) {
	const project::ParamAddress param{.owner = nowhere.kind == project::SlotAddress::Kind::DRUM
	                                               ? project::OwnerId::drum(nowhere.id)
	                                               : project::OwnerId::instrument(nowhere.id)};
	return sound.sound_state(nowhere) == nullptr && sound.cables(param).empty() && sound.loaded_sources(nowhere) == 0
	       && !sound.default_range(nowhere, 0).has_value() && !sound.range_identity(nowhere, 0, 0).has_value()
	       && !sound.kit_takes_whole_folder(nowhere);
}

/// @brief A clip no song holds is no audio clip, does not play, and measures nothing.
[[nodiscard]] inline bool unknown_clip_is_silent(const ClipFacts& clips, project::ClipAddress nowhere) {
	const std::pair<int64_t, int64_t> nothing{0, 0};
	return !clips.audio_clip(nowhere).has_value() && !clips.clip_playing(nowhere)
	       && !clips.clip_recording_linearly(nowhere) && clips.audio_clip_samples(nowhere, 0, 1) == nothing;
}

/// @brief A chain no song holds has no effects.
[[nodiscard]] inline bool unknown_chain_has_no_effects(const TrackFacts& tracks, project::ChainAddress nowhere) {
	return tracks.chain_effects(nowhere) == nullptr;
}

/// @brief Every clip edit on a clip no song holds is refused. Meaningful on the host, which refuses by resolution; a
///        recording fake refuses only with `accept` false, so set that first.
[[nodiscard]] inline bool unknown_clip_is_refused(ClipWrites& writes, project::ClipAddress nowhere) {
	return !writes.recolour_clip(nowhere, 1) && !writes.clear_clip(nowhere, true, true)
	       && !writes.move_audio_clip_marker(nowhere, "endSamplePos", 1)
	       && !writes.resize_audio_clip(nowhere, ResizeKind::NO_STRETCH, 96, 192);
}

/// @brief Every sound edit on a sound no song holds is refused. A fake that accepts everything is set to refuse first.
[[nodiscard]] inline bool unknown_sound_is_refused(SoundWrites& writes, project::SlotAddress nowhere) {
	const instruments::classic::DxPatchState patch{};
	return !writes.write_ranges(nowhere, 0, {}, {}) && !writes.insert_range(nowhere, 0, 0, 40, std::nullopt)
	       && !writes.delete_range(nowhere, 0, 0, std::nullopt)
	       && !writes.attach_recording(nowhere, 0, 0, "NOWHERE.WAV") && !writes.write_dx_patch(nowhere, 0, patch)
	       && !writes.load_dx_program(nowhere, 0, 0)
	       && !writes.write_source_transpose(Addresses{.slot = nowhere}, 0, std::nullopt, 0);
}

/// @brief A velocity default for no device is refused: the host has no cable to read it from. A fake that accepts
///        everything is set to refuse first.
[[nodiscard]] inline bool velocity_of_no_device_is_refused(SoundWrites& writes) {
	return !writes.velocity_to_level_everywhere(midi::DeviceId{});
}

/// @brief A knob learned onto a param of an owner no song holds is refused, a MIDI knob's and a mod knob's alike, and
///        so is a learn of a MIDI command onto that param. A fake that accepts everything is set to refuse first.
[[nodiscard]] inline bool unknown_owner_knob_is_refused(LearnSink& learns, const project::OwnerParamTarget& nowhere) {
	return !learns.learn_knob(nowhere, midi::kDin, 74, 3) && !learns.learn_knob(nowhere, midi::DeviceId{}, 1, 255)
	       && !learns.learn(project::MappingTarget{nowhere},
	                        project::MidiCommand{.channel_or_zone = 1, .note_or_cc = 74}, std::nullopt);
}

/// @brief A menu write on a sound no song holds, and a text write and an automation delete on a clip no song holds,
///        are refused: the host refuses each by resolving its owner. A recording fake refuses only with `accept`
///        false, so set that first.
[[nodiscard]] inline bool unknown_slot_write_is_refused(WriteSink& writes, project::SlotAddress nowhere,
                                                        project::ClipAddress nowhere_clip) {
	const WriteRequest transpose{
	    .target = {.kind = Target::Kind::SOUND, .path = "osc1/transpose"}, .where = {.slot = nowhere}, .menu_value = 1};
	const Target name{.kind = Target::Kind::MODEL, .path = "name", .group = project::SettingGroupId::CLIP};
	const project::ParamAddress param =
	    project::ParamAddress::of(project::OwnerId::clip(nowhere_clip.clip_id), project::engine_target(3));
	return !writes.write(transpose) && !writes.write_text(name, Addresses{.clip = nowhere_clip}, "NOWHERE")
	       && !writes.delete_automation(param);
}

/// @brief A param of an owner no song holds has no value and is not automated.
[[nodiscard]] inline bool unknown_param_has_no_value(const ui::controls::Facts& controls,
                                                     const project::ParamAddress& nowhere) {
	return !controls.param_value(nowhere).has_value() && !controls.param_automated(nowhere);
}

/// @brief A knob's write to, and the making of, a param of an owner no song holds are refused. A recording fake
///        refuses only with `accept` false, so set that first.
[[nodiscard]] inline bool unknown_param_write_is_refused(ui::controls::Sink& sink,
                                                         const project::ParamAddress& nowhere) {
	return !sink.write_param(nowhere, 1, 0, 0, ui::controls::Resolution::KNOB) && !sink.make_param(nowhere);
}

/// @brief Every connected device has a name and settings, and no device names none.
[[nodiscard]] inline bool connected_devices_are_known(const MidiDevices& midi) {
	for (const midi::DeviceId d : midi.connected()) {
		if (!d || midi.name(d).empty() || !midi.settings(d).has_value()) {
			return false;
		}
	}
	return !midi.settings(midi::DeviceId{}).has_value();
}

/// @brief Every connected device has a reference, DIN's names the DIN port, and no device names none.
[[nodiscard]] inline bool connected_devices_have_references(const MidiDevices& midi) {
	for (const midi::DeviceId d : midi.connected()) {
		if (!midi.reference(d).has_value()) {
			return false;
		}
	}
	const std::optional<project::MidiDeviceRef> din = midi.reference(midi::kDin);
	return din.has_value() && din->port == project::MidiDeviceRef::Port::DIN
	       && !midi.reference(midi::DeviceId{}).has_value();
}

/// @brief A write to no device is refused.
[[nodiscard]] inline bool write_to_no_device_is_refused(MidiDevices& midi) {
	return !midi.write(midi::DeviceId{}, MidiDeviceSettings{.send_clock = true});
}

/// @brief A global command's and a follow input's learn reads back through @p midi, one from @p known and one from any
///        device, and a nullopt learn unlearns each. Puts back what it found: the host spec shares its process.
/// @param known A device @p midi knows.
[[nodiscard]] inline bool learns_read_back(LearnSink& learns, const MidiDevices& midi, midi::DeviceId known) {
	constexpr project::GlobalCommand kCommand = project::GlobalCommand::TAP;
	constexpr project::FollowInput kInput = project::FollowInput::TRACK_2;
	const std::optional<LearnedBinding> command_was = midi.global_command(kCommand);
	const std::optional<LearnedBinding> input_was = midi.follow_input(kInput);
	const LearnedBinding cc{
	    .device = known, .channel_or_zone = static_cast<uint8_t>(project::kFirstCcChannel + 3), .note_or_cc = 42};
	const LearnedBinding channel{.channel_or_zone = 5};
	learns.learn_global(kCommand, cc);
	learns.learn_follow(kInput, channel);
	const bool learned = midi.global_command(kCommand) == cc && midi.follow_input(kInput) == channel;
	learns.learn_global(kCommand, std::nullopt);
	learns.learn_follow(kInput, std::nullopt);
	const bool forgot = !midi.global_command(kCommand).has_value() && !midi.follow_input(kInput).has_value();
	learns.learn_global(kCommand, command_was);
	learns.learn_follow(kInput, input_was);
	return learned && forgot && midi.global_command(kCommand) == command_was && midi.follow_input(kInput) == input_was;
}

/// @brief A global command's or a follow input's learn from a device @p midi does not know stores nothing.
[[nodiscard]] inline bool unknown_device_learn_is_refused(LearnSink& learns, const MidiDevices& midi) {
	constexpr project::GlobalCommand kCommand = project::GlobalCommand::PLAY;
	constexpr project::FollowInput kInput = project::FollowInput::A;
	constexpr midi::DeviceId kUnknown{0xFFFF'FFF0};
	const std::optional<LearnedBinding> command_was = midi.global_command(kCommand);
	const std::optional<LearnedBinding> input_was = midi.follow_input(kInput);
	learns.learn_global(kCommand, LearnedBinding{.device = kUnknown, .channel_or_zone = 1, .note_or_cc = 60});
	learns.learn_follow(kInput, LearnedBinding{.device = kUnknown, .channel_or_zone = 2});
	return midi.global_command(kCommand) == command_was && midi.follow_input(kInput) == input_was;
}

/// @brief A follow input learned to a device and no channel, as a settings file can leave it, reads back with its
///        device, and a channel written over it keeps the device. Puts back what it found.
/// @param known A device @p midi knows.
[[nodiscard]] inline bool follow_device_without_channel_reads_back(LearnSink& learns, const MidiDevices& midi,
                                                                   midi::DeviceId known) {
	constexpr project::FollowInput kInput = project::FollowInput::B;
	const std::optional<LearnedBinding> input_was = midi.follow_input(kInput);
	const LearnedBinding device_only{.device = known};
	learns.learn_follow(kInput, device_only);
	const bool kept = midi.follow_input(kInput) == device_only;
	const LearnedBinding with_channel{.device = known, .channel_or_zone = 0};
	learns.learn_follow(kInput, with_channel);
	const bool channel_kept = midi.follow_input(kInput) == with_channel;
	learns.learn_follow(kInput, input_was);
	return kept && channel_kept && midi.follow_input(kInput) == input_was;
}

/// @brief A param no CC can follow has no CC, under either owner.
[[nodiscard]] inline bool unfollowable_param_has_no_cc(const MidiFeedback& feedback) {
	constexpr project::ParamTarget kNowhere = project::engine_target(0xFFFF'FF00);
	return !feedback.cc_of(project::OwnerPlugin::INSTRUMENT, kNowhere).has_value()
	       && !feedback.cc_of(project::OwnerPlugin::GLOBAL_EFFECTABLE, kNowhere).has_value();
}

/// @brief The transport's recording is one of the three modes.
[[nodiscard]] inline bool transport_recording_is_a_mode(const Transport& t) {
	const TransportRecording r = t.recording();
	return r == TransportRecording::OFF || r == TransportRecording::NORMAL || r == TransportRecording::ARRANGEMENT;
}

/// @brief The count-in shows no negative digit.
[[nodiscard]] inline bool count_in_is_a_count(const Transport& t) {
	return t.count_in_visual() >= 0;
}

/// @brief Each metronome toggle reads back through @p t, and two leave it as found. The host shows its popup twice.
[[nodiscard]] inline bool metronome_toggles_read_back(TransportActions& actions, const Transport& t) {
	const bool was = t.metronome_on();
	actions.toggle_metronome();
	const bool flipped = t.metronome_on() != was;
	actions.toggle_metronome();
	return flipped && t.metronome_on() == was;
}

/// @brief The navigation is stable across two calls, and a zoom it gives is at least one tick.
[[nodiscard]] inline bool clip_navigation_is_stable(const ClipFacts& clips) {
	const std::optional<ClipNavigation> a = clips.clip_navigation();
	return a == clips.clip_navigation() && (!a || a->x_zoom >= 1);
}

/// @brief The arrangement mode is stable, and recording to the arrangement implies it.
[[nodiscard]] inline bool arrangement_mode_is_stable(const Transport& t) {
	const bool a = t.arrangement_mode();
	return a == t.arrangement_mode() && (t.recording() != TransportRecording::ARRANGEMENT || a);
}

/// @brief A tempo grab on a clip that is not an audio clip leaves the transport as it was: the song's tempo, which a
///        grab sets, and whether it plays.
/// @param record The song's transport record, whose tick length is the tempo a grab writes.
[[nodiscard]] inline bool grab_tempo_off_an_audio_clip_does_nothing(TransportActions& actions, const Transport& t,
                                                                    const project::Transport& record,
                                                                    project::ClipAddress not_audio) {
	const bool playing = t.playing();
	const uint64_t tempo = record.samples_per_timer_tick_big;
	actions.grab_tempo(not_audio);
	return t.playing() == playing && record.samples_per_timer_tick_big == tempo;
}

/// @brief The foreground recorder's source is one of the four.
[[nodiscard]] inline bool foreground_recording_is_a_source(const AudioFacts& audio) {
	const ForegroundRecording f = audio.foreground_recording();
	return f == ForegroundRecording::UNSET || f == ForegroundRecording::NONE || f == ForegroundRecording::EXTERNAL
	       || f == ForegroundRecording::INTERNAL;
}

/// @brief Forgetting the undo history leaves the transport as it was.
[[nodiscard]] inline bool forgetting_undo_keeps_the_transport(DeviceActions& actions, const Transport& t) {
	const bool playing = t.playing();
	const TransportRecording recording = t.recording();
	const bool metronome = t.metronome_on();
	actions.delete_all_logs();
	return t.playing() == playing && t.recording() == recording && t.metronome_on() == metronome;
}

/// @name The device port's cases. A write case puts back every value it wrote: the host spec shares its process with
///       other specs.
/// @{

/// @brief Every DEVICE row's value, by path, as @p device reads it now.
[[nodiscard]] inline std::vector<std::optional<int32_t>> device_values(const DeviceSettings& device) {
	std::vector<std::optional<int32_t>> values;
	for (const plugin::SettingInfo& row : project::device_table()) {
		values.push_back(device.read(row.path));
	}
	return values;
}

/// @brief The first DEVICE row of @p kind, or null where the group has none.
[[nodiscard]] inline const plugin::SettingInfo* first_device_row(plugin::SettingKind kind) {
	const auto table = project::device_table();
	const auto found = std::ranges::find(table, kind, &plugin::SettingInfo::kind);
	return found != table.end() ? &*found : nullptr;
}

/// @brief A path no DEVICE row has reads as nothing.
[[nodiscard]] inline bool device_unknown_path_reads_nothing(const DeviceSettings& device) {
	return !device.read("no/such/row").has_value() && !device.reader()("no/such/row").has_value();
}

/// @brief A write to a path no DEVICE row has is refused, and no row changes.
[[nodiscard]] inline bool device_unknown_path_write_is_refused(DeviceSettings& device) {
	const auto before = device_values(device);
	return device.write("no/such/row", 0) == DeviceWrite::REFUSED && device_values(device) == before;
}

/// @brief A menu value past a row's range is refused and the row reads as it did: an INTEGER row's max + 1, a CHOICE
///        row's past its last choice, a BOOL row's 2; the group has a row of each kind.
[[nodiscard]] inline bool device_out_of_range_write_is_refused(DeviceSettings& device) {
	const auto before = device_values(device);
	bool refused = true;
	for (const plugin::SettingKind kind :
	     {plugin::SettingKind::INTEGER, plugin::SettingKind::CHOICE, plugin::SettingKind::BOOL}) {
		const plugin::SettingInfo* row = first_device_row(kind);
		refused = refused && row != nullptr
		          && device.write(row->path, plugin::menu_range(*row).second + 1) == DeviceWrite::REFUSED;
	}
	return refused && device_values(device) == before;
}

/// @brief Every menu value of the DEVICE row at @p path is stored and reads back as written, in stored units; the row
///        is put back as it was.
[[nodiscard]] inline bool device_row_reads_back(DeviceSettings& device, std::string_view path) {
	const plugin::SettingInfo* row = plugin::find_setting(project::device_table(), path);
	const std::optional<int32_t> was = device.read(path);
	if (row == nullptr || !was) {
		return false;
	}
	// Every value of a short range; a long one's ends and their neighbours.
	const auto [least, greatest] = plugin::menu_range(*row);
	std::vector<int32_t> menus{least, least + 1, greatest - 1, greatest};
	if (greatest - least <= 3) {
		menus.clear();
		for (int32_t menu = least; menu <= greatest; ++menu) {
			menus.push_back(menu);
		}
	}
	bool read_back = true;
	for (const int32_t menu : menus) {
		const auto stored = plugin::from_menu(*row, menu);
		const DeviceWrite done = device.write(path, menu);
		read_back = read_back && stored && done != DeviceWrite::REFUSED && device.read(path) == stored->first;
	}
	const bool restored = device.write(path, plugin::to_menu(*row, *was)) != DeviceWrite::REFUSED;
	return read_back && restored && device.read(path) == was;
}

/// @brief A write in range is stored and reads back as written: for the first BOOL, INTEGER and CHOICE row, which the
///        group has each of.
[[nodiscard]] inline bool device_write_reads_back(DeviceSettings& device) {
	const plugin::SettingInfo* boolean = first_device_row(plugin::SettingKind::BOOL);
	const plugin::SettingInfo* choice = first_device_row(plugin::SettingKind::CHOICE);
	const plugin::SettingInfo* integer = first_device_row(plugin::SettingKind::INTEGER);
	return boolean != nullptr && choice != nullptr && integer != nullptr && device_row_reads_back(device, boolean->path)
	       && device_row_reads_back(device, choice->path) && device_row_reads_back(device, integer->path);
}

/// @brief The port's reader reads what `read` reads, for the first row of each kind and a path no row has.
[[nodiscard]] inline bool device_reader_reads_the_rows(const DeviceSettings& device) {
	const plugin::SettingReader reader = device.reader();
	bool same = reader("no/such/row") == device.read("no/such/row");
	for (const plugin::SettingKind kind :
	     {plugin::SettingKind::BOOL, plugin::SettingKind::INTEGER, plugin::SettingKind::CHOICE}) {
		const plugin::SettingInfo* row = first_device_row(kind);
		same = same && row != nullptr && reader(row->path).has_value() && reader(row->path) == device.read(row->path);
	}
	return same;
}

/// @brief The typed reads answer what `read` reads: the play cursor as one of its three values, the shortcut layout as
///        one of its two, the favourites layout as one of its three, the automation feedback as one of its four, and a
///        feature's flag as its row's value.
[[nodiscard]] inline bool device_typed_reads_match(const DeviceSettings& device) {
	using device_path = project::device_path;
	const project::FlashCursor cursor = read_device<project::FlashCursor>(&device, device_path::kPlayCursor);
	const project::ShortcutsVersion version =
	    read_device<project::ShortcutsVersion>(&device, device_path::kShortcutsVersion);
	const project::FavouritesLayout favourites =
	    read_device<project::FavouritesLayout>(&device, device_path::kFavouritesLayout);
	const project::FeedbackAutomation automation =
	    read_device<project::FeedbackAutomation>(&device, device_path::kFollowFeedbackAutomation);
	const project::DevicePath battery = device_path::feature(project::Feature::SHOW_BATTERY_LEVEL);
	const bool named_cursor = cursor == project::FlashCursor::FAST || cursor == project::FlashCursor::OFF
	                          || cursor == project::FlashCursor::SLOW;
	const bool named_version = version == project::ShortcutsVersion::V1 || version == project::ShortcutsVersion::V3;
	const bool named_favourites = favourites == project::FavouritesLayout::FAVOURITES
	                              || favourites == project::FavouritesLayout::FAVOURITES_AND_BANKS
	                              || favourites == project::FavouritesLayout::OFF;
	const bool named_automation =
	    automation == project::FeedbackAutomation::DISABLED || automation == project::FeedbackAutomation::LOW
	    || automation == project::FeedbackAutomation::MEDIUM || automation == project::FeedbackAutomation::HIGH;
	return named_cursor && named_version && named_favourites && named_automation
	       && device.read(device_path::kPlayCursor) == std::to_underlying(cursor)
	       && device.read(device_path::kShortcutsVersion) == std::to_underlying(version)
	       && device.read(device_path::kFavouritesLayout) == std::to_underlying(favourites)
	       && device.read(device_path::kFollowFeedbackAutomation) == std::to_underlying(automation)
	       && read_device_flag(&device, battery) == (device.read(battery).value_or(0) != 0);
}

/// @}

/// @name The waveforms port's cases.
/// @{

/// @brief An address nothing is loaded at answers no info, nothing to draw or wait for, and no position.
[[nodiscard]] inline bool waveforms_unknown_address_reads_nothing(const Waveforms& w, const WaveformAddress& nowhere) {
	return !w.info(nowhere).has_value()
	       && w.peaks(nowhere, {.scroll = 0, .zoom = 1}) == views::waveform::PeakRead::all_past_end()
	       && !w.play_position(nowhere).has_value();
}

/// @brief A window that covers no frames has nothing to draw.
[[nodiscard]] inline bool waveforms_empty_window_reads_past_end(const Waveforms& w, const WaveformAddress& at) {
	return w.peaks(at, {.scroll = 0, .zoom = 0}) == views::waveform::PeakRead::all_past_end();
}

/// @brief Over a finished file, a column from the length on is past the end, and a column wholly inside a file of
///        whole clusters is not: pending means "ask again", never "nothing there".
[[nodiscard]] inline bool waveforms_columns_follow_the_length(const Waveforms& w, const WaveformAddress& at) {
	const std::optional<WaveformInfo> info = w.info(at);
	if (!info || info->recording || info->length_samples == 0) {
		return false;
	}
	const auto length = static_cast<int64_t>(info->length_samples);
	// Twice the whole-waveform zoom, so about half the columns lie past the end.
	const int64_t zoom = std::max<int64_t>(1, base::ceil_div(length, views::waveform::kColumns) << 1);
	const views::waveform::PeakRead read = w.peaks(at, {.scroll = 0, .zoom = zoom});
	for (int32_t c = 0; c < views::waveform::kColumns; c++) {
		// Past the end exactly where the column starts at or past the length.
		const bool past_end = read.column(c).state() == views::waveform::ColumnState::PAST_END;
		if ((int64_t{c} * zoom >= length) != past_end) {
			return false;
		}
	}
	return true;
}

/// @brief Over a finished file, a column once answered with a peak answers the same peak on the next read, and the
///        second read has a peak to draw.
[[nodiscard]] inline bool waveforms_peaks_are_stable(const Waveforms& w, const WaveformAddress& at) {
	const std::optional<WaveformInfo> info = w.info(at);
	if (!info || info->recording) {
		return false;
	}
	const views::waveform::Window window{
	    .scroll = 0,
	    .zoom = std::max<int64_t>(
	        1, base::ceil_div(static_cast<int64_t>(info->length_samples), views::waveform::kColumns))};
	const views::waveform::PeakRead first = w.peaks(at, window);
	const views::waveform::PeakRead second = w.peaks(at, window);
	bool drawn = false;
	for (int32_t c = 0; c < views::waveform::kColumns; c++) {
		if (first.column(c).peak() && first.column(c) != second.column(c)) {
			return false;
		}
		drawn = drawn || second.column(c).peak().has_value();
	}
	return drawn;
}

/// @brief Two infos of one waveform agree, and a loaded file's facts are consistent.
[[nodiscard]] inline bool waveforms_info_is_consistent(const Waveforms& w, const WaveformAddress& at) {
	const std::optional<WaveformInfo> a = w.info(at);
	const std::optional<WaveformInfo> b = w.info(at);
	return a.has_value() && a == b && (a->channels == 1 || a->channels == 2) && a->identity != 0;
}

/// @}

/// @name The input take's cases.
/// @{

/// @brief With no take held, a take's state and path read none.
[[nodiscard]] inline bool no_take_reads_nothing(const AudioFacts& audio) {
	return !audio.input_take().has_value() && !audio.input_take_path().has_value();
}

/// @brief The audio clock does not run backwards between two calls.
[[nodiscard]] inline bool audio_clock_is_monotonic(const AudioFacts& audio) {
	const uint32_t a = audio.audio_sample_timer();
	return audio.audio_sample_timer() - a < 0x8000'0000U;
}

/// @brief With no take held, every take call is harmless: it reports no clip and no level, and the state and path
///        still read none afterwards.
[[nodiscard]] inline bool take_calls_without_a_take_do_nothing(TakeActions& takes, const AudioFacts& audio) {
	takes.end_soon();
	const bool clipped = takes.take_clipped();
	const InputLevels levels = takes.take_levels();
	takes.finish();
	takes.orphan();
	return !clipped && levels == InputLevels{} && no_take_reads_nothing(audio);
}

/// @}

/// @name The indicators' and the root view's cases.
/// @{

/// @brief Clearing the tick squares changes no fact of the transport.
[[nodiscard]] inline bool clearing_ticks_keeps_the_transport(Indicators& indicators, const Transport& transport) {
	const auto facts = [&] {
		return std::tuple{transport.playing(),      transport.clocks_active(),    transport.recording(),
		                  transport.metronome_on(), transport.overdubs_pending(), transport.count_in_visual()};
	};
	const auto before = facts();
	indicators.clear_tick_squares();
	return facts() == before;
}

/// @brief With nothing auditioning, ending an audition of every kind changes no fact of the UI.
[[nodiscard]] inline bool ending_no_audition_changes_nothing(ViewActions& view, const UiFacts& ui) {
	if (ui.auditioning()) {
		return false;
	}
	const auto facts = [&] { return std::tuple{ui.auditioning(), ui.popup_showing(), ui.performance_editing_mode()}; };
	const auto before = facts();
	for (const AuditionEnd how : {AuditionEnd::ClipViewWhenAuditioning, AuditionEnd::ClipViewForInstrumentClip,
	                              AuditionEnd::KeyboardWhenAuditioning}) {
		view.end_audition(how);
		if (facts() != before) {
			return false;
		}
	}
	return true;
}

/// @}

/// @brief No entry is answered past the end, and erasing past the end changes nothing.
[[nodiscard]] inline bool catalogue_ends_at_its_count(Catalogue& catalogue) {
	const std::size_t count = catalogue.count();
	if (catalogue.entry(count).has_value()) {
		return false;
	}
	catalogue.erase(count);
	return catalogue.count() == count;
}

/// @brief Erasing an entry moves the one after it into its place. Needs at least two entries.
[[nodiscard]] inline bool catalogue_erase_closes_the_gap(Catalogue& catalogue) {
	if (catalogue.count() < 2) {
		return false;
	}
	const std::string after{catalogue.entry(1)->name};
	const std::size_t count = catalogue.count();
	catalogue.erase(0);
	return catalogue.count() == count - 1 && catalogue.entry(0)->name == after;
}

/// @brief Clearing leaves no entry.
[[nodiscard]] inline bool catalogue_clear_empties(Catalogue& catalogue) {
	catalogue.clear();
	return catalogue.count() == 0 && !catalogue.entry(0).has_value();
}

/// @brief One owner holds the claim at a time; a release by anyone else leaves the holder, and the holder's own
///        release frees it. The caller holds no claim on entry, and none is left held.
[[nodiscard]] inline bool catalogue_claim_is_single(Catalogue& catalogue) {
	int first = 0;
	int second = 0;
	if (!catalogue.claim(&first)) {
		return false;
	}
	const bool refused = !catalogue.claim(&second);
	catalogue.release(&second);
	const bool still_held = !catalogue.claim(&second);
	catalogue.release(&first);
	const bool freed = catalogue.claim(&second);
	catalogue.release(&second);
	return refused && still_held && freed;
}

/// @brief A name sorting after every entry is found at the count, and not exactly.
[[nodiscard]] inline bool catalogue_search_past_the_end(Catalogue& catalogue) {
	const SearchHit hit = catalogue.search("\x7f\x7f\x7f");
	return hit.index == catalogue.count() && !hit.exact;
}

} // namespace deluge::ui::scenes::bind::contract
