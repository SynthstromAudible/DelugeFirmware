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

namespace deluge::gui::menu_item::lfo {

class Sync final : public SyncLevel {
public:
	Sync(deluge::l10n::String name, deluge::l10n::String type, uint8_t lfoId) : SyncLevel(name, type), lfoId_(lfoId) {}

	void readCurrentValue() override {
		this->setValue(syncTypeAndLevelToMenuOption(soundEditor.currentSound->lfoConfig[lfoId_].syncType,
		                                            soundEditor.currentSound->lfoConfig[lfoId_].syncLevel));
	}
	void writeCurrentValue() override {
		soundEditor.currentSound->lfoConfig[lfoId_].syncType = syncValueToSyncType(this->getValue());
		soundEditor.currentSound->lfoConfig[lfoId_].syncLevel = syncValueToSyncLevel(this->getValue());
		// This fires unnecessarily for LFO2 assignments as well, but that's ok. It's not
		// entirely clear if we really need this for the LFO1, even: maybe the clock-driven resyncs
		// would be enough?
		soundEditor.currentSound->resyncGlobalLFO();
		soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
	}

private:
	uint8_t lfoId_;
};

} // namespace deluge::gui::menu_item::lfo
