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
#include "definitions.h"
#include "gui/menu_item/sync_level.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::lfo::global {

class Sync final : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

	void readCurrentValue() {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentSound->lfoGlobalSyncType,
		                                                        soundEditor.currentSound->lfoGlobalSyncLevel);
	}
	void writeCurrentValue() {
		soundEditor.currentSound->setLFOGlobalSyncType(menuOptionToSyncType(soundEditor.currentValue));
		soundEditor.currentSound->setLFOGlobalSyncLevel(menuOptionToSyncLevel(soundEditor.currentValue));
		soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
	}
};

} // namespace menu_item::lfo::global
