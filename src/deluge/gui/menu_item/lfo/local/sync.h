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
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::lfo::local {

class Sync final : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

	void readCurrentValue() {
		this->setValue(syncTypeAndLevelToMenuOption(soundEditor.currentSound->localLFOConfig.syncType,
		                                            soundEditor.currentSound->localLFOConfig.syncLevel));
	}
	void writeCurrentValue() {
		soundEditor.currentSound->localLFOConfig.syncType = menuOptionToSyncType(this->getValue());
		soundEditor.currentSound->localLFOConfig.syncLevel = menuOptionToSyncLevel(this->getValue());
		// XXX: Do we need this call for the local LFO?
		soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
	}
};

} // namespace deluge::gui::menu_item::lfo::local
