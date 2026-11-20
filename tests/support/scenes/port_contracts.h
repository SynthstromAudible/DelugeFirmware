#pragma once

/// @file
/// @brief Cases every implementation of a port must pass, the fake and the host's alike. Each takes the port under
///        test and returns whether it holds; a spec states the case and expects true.

#include "deluge-project/device_settings.h"
#include "plugin-api/types/display_mapping.h"
#include "scenes/bind/device_reader.h"
#include "scenes/bind/ports/ports.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::scenes::bind::contract {

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
	       && card.output_may_be_replaced() == card.output_may_be_replaced();
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
	const instrument::DxPatchState patch{};
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
[[nodiscard]] inline bool unknown_param_has_no_value(const controls::Facts& controls,
                                                     const project::ParamAddress& nowhere) {
	return !controls.param_value(nowhere).has_value() && !controls.param_automated(nowhere);
}

/// @brief A knob's write to, and the making of, a param of an owner no song holds are refused. A recording fake
///        refuses only with `accept` false, so set that first.
[[nodiscard]] inline bool unknown_param_write_is_refused(controls::Sink& sink, const project::ParamAddress& nowhere) {
	return !sink.write_param(nowhere, 1, 0, 0, controls::Resolution::KNOB) && !sink.make_param(nowhere);
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

/// @brief A write to no device is refused.
[[nodiscard]] inline bool write_to_no_device_is_refused(MidiDevices& midi) {
	return !midi.write(midi::DeviceId{}, MidiDeviceSettings{.send_clock = true});
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

/// @brief The first DEVICE row of @p kind, or null where the group has none yet.
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
///        row's past its last choice, a BOOL row's 2, each kind the group has.
[[nodiscard]] inline bool device_out_of_range_write_is_refused(DeviceSettings& device) {
	const auto before = device_values(device);
	bool refused = true;
	for (const plugin::SettingKind kind :
	     {plugin::SettingKind::INTEGER, plugin::SettingKind::CHOICE, plugin::SettingKind::BOOL}) {
		const plugin::SettingInfo* row = first_device_row(kind);
		if (row == nullptr) {
			continue;
		}
		const int32_t past = plugin::menu_range(*row).second + 1;
		refused = refused && device.write(row->path, past) == DeviceWrite::REFUSED;
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

/// @brief A write in range is stored and reads back as written: for the first BOOL, INTEGER and CHOICE row, each kind
///        the group has, and the BOOL and CHOICE kinds at least.
[[nodiscard]] inline bool device_write_reads_back(DeviceSettings& device) {
	const plugin::SettingInfo* boolean = first_device_row(plugin::SettingKind::BOOL);
	const plugin::SettingInfo* choice = first_device_row(plugin::SettingKind::CHOICE);
	const plugin::SettingInfo* integer = first_device_row(plugin::SettingKind::INTEGER);
	return boolean != nullptr && choice != nullptr && device_row_reads_back(device, boolean->path)
	       && device_row_reads_back(device, choice->path)
	       && (integer == nullptr || device_row_reads_back(device, integer->path));
}

/// @brief The port's reader reads what `read` reads, for the first row of each kind and a path no row has.
[[nodiscard]] inline bool device_reader_reads_the_rows(const DeviceSettings& device) {
	const plugin::SettingReader reader = device.reader();
	bool same = reader("no/such/row") == device.read("no/such/row");
	for (const plugin::SettingKind kind :
	     {plugin::SettingKind::BOOL, plugin::SettingKind::INTEGER, plugin::SettingKind::CHOICE}) {
		if (const plugin::SettingInfo* row = first_device_row(kind)) {
			same = same && reader(row->path).has_value() && reader(row->path) == device.read(row->path);
		}
	}
	return same;
}

/// @brief The typed reads answer what `read` reads: the play cursor as one of its three values, the shortcut layout as
///        one of its two, and a feature's flag as its row's value.
[[nodiscard]] inline bool device_typed_reads_match(const DeviceSettings& device) {
	namespace device_path = project::device_path;
	const project::FlashCursor cursor = read_device<project::FlashCursor>(&device, device_path::kPlayCursor);
	const project::ShortcutsVersion version =
	    read_device<project::ShortcutsVersion>(&device, device_path::kShortcutsVersion);
	const std::string_view battery = device_path::feature(project::Feature::SHOW_BATTERY_LEVEL);
	const bool named_cursor = cursor == project::FlashCursor::FAST || cursor == project::FlashCursor::OFF
	                          || cursor == project::FlashCursor::SLOW;
	const bool named_version = version == project::ShortcutsVersion::V1 || version == project::ShortcutsVersion::V3;
	return named_cursor && named_version && device.read(device_path::kPlayCursor) == std::to_underlying(cursor)
	       && device.read(device_path::kShortcutsVersion) == std::to_underlying(version)
	       && read_device_flag(&device, battery) == (device.read(battery).value_or(0) != 0);
}

/// @}

} // namespace deluge::scenes::bind::contract
