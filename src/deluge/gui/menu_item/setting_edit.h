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
#include "model/edits/edit.h"
#include "model/edits/set_controls.h"
#include "model/edits/set_dx_patch.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

#include <optional>
#include <string>
#include <string_view>

namespace deluge::gui::menu_item {

/// @brief @p sound's slot when it is bound in the current song, so an Edit on that slot reaches it; nullopt for a
/// sound with no slot or one outside the song (a preview, a preset's sound still being built).
inline std::optional<deluge::project::SlotAddress> songSlotOf(::Sound& sound) {
	const std::optional<deluge::project::SlotAddress> slot = deluge::bridge::slotAddressOf(sound);
	if (slot && deluge::bridge::soundAt(*currentSong, *slot) == &sound) {
		return slot;
	}
	return std::nullopt;
}

/// @brief The settings path of oscillator @p source's (0 or 1) setting @p name: `oscN/name`.
inline std::string oscSettingPath(int32_t source, std::string_view name) {
	return "osc" + std::to_string(source + 1) + "/" + std::string(name);
}

/// @brief A sound editor menu's change of @p sound's setting at @p path to @p value (its file form): an Edit on the
/// sound's slot when the sound is bound in the current song, so the slot keeps holding its state; the runtime's setter
/// alone for a sound with no slot in it.
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

/// @brief A gesture's change of source @p s of @p sound to @p ranges: an Edit on the sound's slot when it is bound,
/// the in-place change alone for a sound with no slot.
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
inline void recordRanges(::Sound& sound, int32_t s) {
	rangesFor(sound, s, deluge::bridge::rangesOf(sound.sources[s]));
}

/// @brief A gesture's change of source @p s of @p sound to the DX7 patch @p patch: an Edit on the sound's slot when it
/// is bound in the current song, the patch alone for a sound with no slot in it.
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

} // namespace deluge::gui::menu_item
