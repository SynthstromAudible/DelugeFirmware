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
#include "model/edits/arp_setting_for.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::randomizer {
class RandomizerLock final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->randomizerLock); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		const deluge::serialization::Value value =
		    deluge::serialization::Value::makeNumber(this->getValue() != 0 ? 1 : 0);

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			for (Drum* thisDrum = getCurrentKit()->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				deluge::edits::laneArpSettingFor(*thisDrum, "arpeggiator/randomizerLock", value);
			}
		}
		// Or, the normal case of just one sound
		else {
			deluge::edits::arpSettingFor(*soundEditor.currentArpSettings, "arpeggiator/randomizerLock", value);
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

	void getColumnLabel(etl::istring& label) override { label.append(deluge::l10n::get(this->name)); }

	// flag this selection menu as a toggle menu so we can use a checkbox to toggle value
	bool isToggle() override { return true; }

	// don't enter menu on select button press
	bool shouldEnterSubmenu() override { return false; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		using namespace deluge::hid::display;
		const Icon& icon = getValue() ? OLED::lockedIconBig : OLED::unlockedIconBig;
		OLED::main.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y - 1);
	}
};
} // namespace deluge::gui::menu_item::randomizer
