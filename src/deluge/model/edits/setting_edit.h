/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "io/debug/log.h"
#include "model/edits/change_effect.h"
#include "model/edits/edit.h"
#include "model/edits/set_controls.h"
#include "model/edits/set_dx_patch.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/setting_projection.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "serialization/tree.h"
#include "serialization/value.h"

#include <optional>
#include <string>
#include <string_view>

/// @file
/// @brief An editor's change of a sound's or an effect owner's setting, as an Edit when the owner is bound in the
///        current song. Shared by the legacy menus and the ported scenes, so both write the same way.

namespace deluge::edits {

/// @brief @p sound's slot when it is bound in the current song, so an Edit on that slot reaches it; nullopt for a
/// sound with no slot or one outside the song (a preview, a preset's sound still being built).
/// @param sound The sound to look up.
/// @return The sound's slot address in the current song, or `std::nullopt`.
inline std::optional<deluge::project::SlotAddress> songSlotOf(::Sound& sound) {
	const std::optional<deluge::project::SlotAddress> slot = deluge::bridge::slotAddressOf(sound);
	if (slot && deluge::bridge::soundAt(*currentSong, *slot) == &sound) {
		return slot;
	}
	return std::nullopt;
}

/// @brief The settings path of oscillator @p source's (0 or 1) setting @p name: `oscN/name`.
/// @param source Oscillator index (0 or 1).
/// @param name   Setting name within the oscillator.
/// @return The path string, with `N` = @p source + 1.
inline std::string oscSettingPath(int32_t source, std::string_view name) {
	return "osc" + std::to_string(source + 1) + "/" + std::string(name);
}

/// @brief A sound editor menu's change of @p sound's setting at @p path to @p value (its file form): an Edit on the
/// sound's slot when the sound is bound in the current song, so the slot keeps holding its state; the runtime's setter
/// alone for a sound with no slot in it.
/// @param sound The sound being edited.
/// @param path  Settings path of the setting.
/// @param value The new value, in its file form.
inline void settingFor(::Sound& sound, std::string_view path, deluge::serialization::Value value) {
	if (const std::optional<deluge::project::SlotAddress> slot = songSlotOf(sound)) {
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		deluge::edits::SetEngineSetting edit{*slot, std::string(path), value};
		if (deluge::edits::apply(ctx, edit) != Error::NONE) {
			// A menu offers only what the setting takes; a refusal here is a menu and schema out of step.
			D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
		}
		return;
	}
	if (deluge::bridge::applySetting(sound, path, value, *currentSong) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A menu's change of @p owner's effect setting at @p path to @p value (its Edit form): see changeEffect.
/// @param owner The effect owner being edited.
/// @param path  Settings path of the effect setting.
/// @param value The new value.
inline void effectSettingFor(ModControllableAudio& owner, std::string_view path, deluge::serialization::Value value) {
	deluge::edits::changeEffect(owner, path, std::move(value));
}

/// @brief A gesture's change of source @p s of @p sound to @p ranges: an Edit on the sound's slot when it is bound,
/// the in-place change alone for a sound with no slot.
/// @param sound  The sound being edited.
/// @param s      Source (oscillator) index.
/// @param ranges The source's new ranges.
inline void rangesFor(::Sound& sound, int32_t s, std::vector<deluge::instrument::RangeState> ranges) {
	if (const std::optional<deluge::project::SlotAddress> slot = songSlotOf(sound)) {
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		deluge::edits::SetSourceRanges edit{*slot, static_cast<uint8_t>(s), std::move(ranges)};
		if (deluge::edits::apply(ctx, edit) != Error::NONE) {
			D_PRINTLN("ranges refused: osc%d", static_cast<int>(s + 1));
		}
		return;
	}
	if (deluge::bridge::applyRanges(sound, s, ranges) != Error::NONE) {
		D_PRINTLN("ranges refused: osc%d", static_cast<int>(s + 1));
	}
}

/// @brief Record source @p s's ranges as a gesture left them, after it loaded files or reshaped the list itself.
/// @param sound The sound whose source changed.
/// @param s     Source (oscillator) index.
inline void recordRanges(::Sound& sound, int32_t s) {
	rangesFor(sound, s, deluge::bridge::rangesOf(sound.sources[s]));
}

/// @brief A gesture's change of source @p s of @p sound to the DX7 patch @p patch: an Edit on the sound's slot when it
/// is bound in the current song, the patch alone for a sound with no slot in it.
/// @param sound The sound being edited.
/// @param s     Source (oscillator) index.
/// @param patch The new DX7 patch state.
inline void dxPatchFor(::Sound& sound, int32_t s, const deluge::instrument::DxPatchState& patch) {
	if (const std::optional<deluge::project::SlotAddress> slot = songSlotOf(sound)) {
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		deluge::edits::SetDxPatch edit{*slot, static_cast<uint8_t>(s), patch};
		if (deluge::edits::apply(ctx, edit) != Error::NONE) {
			D_PRINTLN("dx patch refused: osc%d", static_cast<int>(s + 1));
		}
		return;
	}
	if (deluge::bridge::applyDxPatch(sound, s, patch) != Error::NONE) {
		D_PRINTLN("dx patch refused: osc%d", static_cast<int>(s + 1));
	}
}

/// @brief Record @p owner's knobs as a gesture left them (a learn, an unlearn, a knob cycled to its next param): an
/// Edit on its controller mappings when it is one of the current song's owners; nothing for one outside it (a MIDI or
/// CV track's, which has no knob mappings).
/// @param owner The mod-controllable whose knob mappings changed.
inline void recordControlsOf(::ModControllable& owner) {
	const std::optional<deluge::project::ControlsAddress> address =
	    deluge::bridge::controlsAddressOf(owner, *currentSong);
	if (!address) {
		return;
	}
	bool isSound = false;
	::ModControllableAudio* audio = deluge::bridge::controlsOwnerAt(*currentSong, *address, &isSound);
	if (audio == nullptr) {
		return;
	}
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::SetControls edit{*address, isSound ? deluge::bridge::controlsOf(static_cast<::Sound&>(*audio))
	                                                  : deluge::bridge::learnedControlsOf(*audio)};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		D_PRINTLN("controls refused");
	}
}

/// @brief Apply a `SetSetting` to the current song, logging a refusal: a menu offers only what a setting takes, so a
///        refusal is a menu and its table out of step.
/// @param address The group and owner.
/// @param path    The setting's path.
/// @param value   The new value, in its file form.
inline void applySettingEdit(const deluge::project::SettingAddress& address, std::string_view path,
                             deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::SetSetting edit{address, std::string(path), value};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief Whether @p group's row at @p path refuses @p value, as writing the record would: checked before an owner
///        outside the song takes the value through its setter alone. Logs a refusal.
inline bool settingRefused(deluge::project::SettingGroupId group, std::string_view path,
                           const deluge::serialization::Value& value) {
	const std::string* text = deluge::serialization::text_of(&value);
	if (text != nullptr && deluge::engine::check_setting_text(deluge::project::table_of(group), path, *text)) {
		return false;
	}
	D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	return true;
}

/// @brief A gesture's change of the current song's own setting at @p path (reverb, swing interval, disabled scales).
/// @param path  The setting's path in the song group.
/// @param value The new value, in its file form.
inline void songSettingFor(std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	applySettingEdit({deluge::project::SettingGroupId::SONG, deluge::project::SongAddress{}}, path, std::move(value));
}

/// @brief A menu's change of the current song's setting at @p path, in the menu's units: the row's user scale
///        (`engine::to_stored`) gives the value the record holds.
/// @param path The setting's path in the song group.
/// @param user The menu's value.
inline void songSettingFromMenu(std::string_view path, int32_t user) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	for (const deluge::engine::SettingInfo& row : deluge::project::table_of(deluge::project::SettingGroupId::SONG)) {
		if (row.path == path) {
			if (const std::optional<int32_t> stored = deluge::engine::to_stored(row, user)) {
				songSettingFor(path, deluge::serialization::Value::makeNumber(*stored));
				return;
			}
			break;
		}
	}
	D_PRINTLN("setting refused: %.*s = %d", static_cast<int>(path.size()), path.data(), static_cast<int>(user));
}

/// @brief A gesture's change of @p output's setting at @p path (its name, or its MIDI or CV instrument's fields): an
///        Edit when the output is the current song's, its setter alone for one outside it.
/// @param output The track's output.
/// @param path   The setting's path in the track or instrument-device group.
/// @param value  The new value, in its file form.
inline void trackSettingFor(::Output& output, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::TrackAddress> track =
	        deluge::bridge::trackAddressOf(output, *currentSong)) {
		applySettingEdit({deluge::bridge::trackGroupOf(path), *track}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::bridge::trackGroupOf(path), path, value)) {
		return;
	}
	if (deluge::bridge::applyTrackSetting(output, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A gesture's change of @p drum's lane setting at @p path (its name): an Edit when the drum is in one of the
///        current song's kits, its setter alone otherwise.
/// @param drum  The lane's drum.
/// @param path  The setting's path in the lane group.
/// @param value The new value, in its file form.
inline void laneSettingFor(::Drum& drum, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::LaneAddress> lane = deluge::bridge::laneAddressOf(drum, *currentSong)) {
		applySettingEdit({deluge::project::SettingGroupId::LANE, *lane}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::project::SettingGroupId::LANE, path, value)) {
		return;
	}
	if (deluge::bridge::applyLaneSetting(drum, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A gesture's change of @p row's setting at @p path (its direction): an Edit when the row is addressable in
///        the current song, its setter alone otherwise (a non-sound kit row).
/// @param row   The note row.
/// @param clip  The clip holding it.
/// @param path  The setting's path in the note-row group.
/// @param value The new value, in its file form.
inline void noteRowSettingFor(::NoteRow& row, ::InstrumentClip& clip, std::string_view path,
                              deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::NoteRowAddress> address =
	        deluge::bridge::noteRowAddressOf(row, clip, *currentSong)) {
		applySettingEdit({deluge::project::SettingGroupId::NOTE_ROW, *address}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::project::SettingGroupId::NOTE_ROW, path, value)) {
		return;
	}
	if (deluge::bridge::applyNoteRowSetting(row, clip, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A gesture's change of @p clip's descriptor setting at @p path: an Edit when the clip is the current
///        song's, its setter alone otherwise.
/// @param clip  The audio clip.
/// @param path  The setting's path in the audio-clip group.
/// @param value The new value, in its file form.
inline void audioClipSettingFor(::AudioClip& clip, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::ClipAddress> address =
	        deluge::bridge::audioClipAddressOf(clip, *currentSong)) {
		applySettingEdit({deluge::project::SettingGroupId::AUDIO_CLIP, *address}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::project::SettingGroupId::AUDIO_CLIP, path, value)) {
		return;
	}
	if (deluge::bridge::applyAudioClipSetting(clip, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A gesture's change of @p clip's setting at @p path (its name, its direction): an Edit when the clip is the
///        current song's, its setter alone otherwise.
/// @param clip  The clip, of either type.
/// @param path  The setting's path in the clip group.
/// @param value The new value, in its file form.
inline void clipSettingFor(::Clip& clip, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::ClipAddress> address = deluge::bridge::clipAddressOf(clip, *currentSong)) {
		applySettingEdit({deluge::project::SettingGroupId::CLIP, *address}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::project::SettingGroupId::CLIP, path, value)) {
		return;
	}
	if (deluge::bridge::applyClipSetting(clip, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A gesture's change of the value @p clip holds at @p path over its track's (its MIDI program, its bend
///        ranges): an Edit when the clip is the current song's, its setter alone otherwise.
/// @param clip  The instrument clip.
/// @param path  The setting's path in the clip-value group.
/// @param value The new value, in its file form.
inline void clipValueSettingFor(::InstrumentClip& clip, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::ClipAddress> address = deluge::bridge::clipAddressOf(clip, *currentSong)) {
		applySettingEdit({deluge::project::SettingGroupId::CLIP_VALUES, *address}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::project::SettingGroupId::CLIP_VALUES, path, value)) {
		return;
	}
	if (deluge::bridge::applyClipValueSetting(clip, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

/// @brief A gesture's change of @p output's input at @p path (its mode, its channel, the track it records from): an
///        Edit when the track is the current song's, its setter alone otherwise.
/// @param output The audio track.
/// @param path   The setting's path in the audio-input group.
/// @param value  The new value, in its file form.
inline void audioInputSettingFor(::AudioOutput& output, std::string_view path, deluge::serialization::Value value) {
	if (currentSong == nullptr) {
		D_PRINTLN("setting refused, no song: %.*s", static_cast<int>(path.size()), path.data());
		return;
	}
	if (const std::optional<deluge::project::TrackAddress> track =
	        deluge::bridge::trackAddressOf(output, *currentSong)) {
		applySettingEdit({deluge::project::SettingGroupId::AUDIO_INPUT, *track}, path, std::move(value));
		return;
	}
	if (settingRefused(deluge::project::SettingGroupId::AUDIO_INPUT, path, value)) {
		return;
	}
	if (deluge::bridge::applyAudioInputSetting(output, *currentSong, path, value) != Error::NONE) {
		D_PRINTLN("setting refused: %.*s = %s", static_cast<int>(path.size()), path.data(), value.str().c_str());
	}
}

} // namespace deluge::edits
