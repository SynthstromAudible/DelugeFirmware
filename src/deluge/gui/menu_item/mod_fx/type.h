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
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::mod_fx {

class Type : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->modFXType_); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ModFXType>();
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			bool some_error = false;
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					if (!soundDrum->setModFXType(current_value)) {
						some_error = true;
					}
				}
			}
			if (some_error) {
				display->displayError(Error::INSUFFICIENT_RAM);
			}
		}
		// Or, the normal case of just one sound
		else {
			if (!soundEditor.currentModControllable->setModFXType(current_value)) {
				display->displayError(Error::INSUFFICIENT_RAM);
			}
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
		OLED::main.drawScreenTitle(l10n::getView(l10n::String::STRING_FOR_MOD_FX_TYPE)); // draw title with separator
		drawPixelsForOled();
	}
};
} // namespace deluge::gui::menu_item::mod_fx
