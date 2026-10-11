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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/setting_edit.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/mod_controllable/mod_fx_names.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::mod_fx {

class Type : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(soundEditor.currentModControllable->modControllableSnapshot().effects.mod_fx.type);
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ModFXType>();
		const deluge::serialization::Value value =
		    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(current_value)));
		// A ring type whose ring cannot be allocated keeps its type and passes the audio through; the next effects edit
		// retries. If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					effectSettingFor(*static_cast<SoundDrum*>(thisDrum), "modFXType", value);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			effectSettingFor(*soundEditor.currentModControllable, "modFXType", value);
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return modfx::getModNames();
	}

	[[nodiscard]] int32_t getOccupiedSlots() const override {
		// Occupy the whole page in the horizontal menu
		return 4;
	}

	[[nodiscard]] bool showNotification() const override { return false; }
	[[nodiscard]] bool showColumnLabel() const override { return false; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		OLED::main.drawHorizontalLine(kScreenTitleSeparatorY, 0, OLED_MAIN_WIDTH_PIXELS - 1);
		drawPixelsForOled();
	}
};
} // namespace deluge::gui::menu_item::mod_fx
