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

#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "serialization/value.h"

#include <optional>
#include <string>
#include <string_view>

namespace deluge::gui::menu_item {

/// @brief A sound editor menu's change of @p sound's setting at @p path to @p value (its file form): an Edit on the
/// sound's slot when the sound is bound in the current song, so the slot keeps holding its state; the runtime's setter
/// alone for a sound with no slot.
inline void settingFor(::Sound& sound, std::string_view path, deluge::serialization::Value value) {
	if (const std::optional<deluge::project::SlotAddress> slot = deluge::bridge::slotAddressOf(sound)) {
		deluge::edits::Context ctx{*currentSong, currentSong->project};
		deluge::edits::SetEngineSetting edit{*slot, std::string(path), std::move(value)};
		(void)deluge::edits::apply(ctx, edit);
		return;
	}
	(void)deluge::bridge::applySetting(sound, path, value, *currentSong);
}

} // namespace deluge::gui::menu_item
