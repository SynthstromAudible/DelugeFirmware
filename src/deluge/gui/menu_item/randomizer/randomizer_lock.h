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
#include "gui/l10n/strings.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::randomizer {
class RandomizerLock final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->randomizerLock); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		bool current_value = this->getValue() != 0;

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				thisDrum->arpSettings.randomizerLock = current_value;
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentArpSettings->randomizerLock = current_value;
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF), //<
		    l10n::getView(STRING_FOR_ON),  //<
		};
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Selection::configureRenderingOptions(options);
		options.label = deluge::l10n::get(deluge::l10n::built_in::seven_segment, this->name);
	}

	// flag this selection menu as a toggle menu so we can use a checkbox to toggle value
	bool isToggle() override { return true; }

	// don't enter menu on select button press
	bool shouldEnterSubmenu() override { return false; }

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		using namespace deluge::hid::display;
		const Icon& icon = getValue() ? OLED::randomizerLockOnIcon : OLED::randomizerLockOffIcon;
		OLED::main.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y - 1);
	}
};
} // namespace deluge::gui::menu_item::randomizer
