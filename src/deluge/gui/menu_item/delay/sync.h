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
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::delay {
class Sync final : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

	void readCurrentValue() override {
		this->setValue(syncTypeAndLevelToMenuOption(soundEditor.currentModControllable->delay.syncType,
		                                            soundEditor.currentModControllable->delay.syncLevel));
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->delay.syncType = syncValueToSyncType(current_value);
					soundDrum->delay.syncLevel = syncValueToSyncLevel(current_value);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentModControllable->delay.syncType = syncValueToSyncType(current_value);
			soundEditor.currentModControllable->delay.syncLevel = syncValueToSyncLevel(current_value);
		}
	}
};
} // namespace deluge::gui::menu_item::delay
