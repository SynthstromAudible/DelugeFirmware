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
#include "gui/menu_item/setting_edit.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "plugin-api/types/sync.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::lfo {

class Sync final : public SyncLevel, FormattedTitle {
public:
	Sync(l10n::String name, l10n::String title, uint8_t lfoId)
	    : SyncLevel(name, title), FormattedTitle(title, lfoId + 1), lfoId_(lfoId) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		this->setValue(syncTypeAndLevelToMenuOption(soundEditor.currentSound->snapshot().lfoConfig[lfoId_].syncType,
		                                            soundEditor.currentSound->snapshot().lfoConfig[lfoId_].syncLevel));
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
					setSync(*soundDrum, current_value);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			setSync(*soundEditor.currentSound, current_value);
		}
	}

private:
	/// @brief Set @p sound's LFO to the menu's @p option (a sync type and level together) as two setting Edits.
	///
	/// @param sound  The sound whose LFO is set.
	/// @param option The menu option, encoding sync type and level together.
	void setSync(Sound& sound, int32_t option) const {
		const std::string prefix = "lfo" + std::to_string(lfoId_ + 1) + "/";
		settingFor(sound, prefix + "syncType",
		           serialization::Value::makeNumber(std::to_underlying(syncValueToSyncType(option))));
		settingFor(sound, prefix + "syncLevel",
		           serialization::Value::makeNumber(deluge::plugin::index_of(syncValueToSyncLevel(option))));
	}

	uint8_t lfoId_;
};

} // namespace deluge::gui::menu_item::lfo
