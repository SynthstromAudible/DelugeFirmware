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
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/selection.h"
#include "off_time.h"
#include "gui/ui/sound_editor.h"
#include "mode.h"

extern deluge::gui::menu_item::gate::OffTime gateOffTimeMenu;
extern deluge::gui::menu_item::gate::Mode gateModeMenu;
namespace deluge::gui::menu_item::gate {

class Selection final : public menu_item::Selection {
public:
	Selection(char const* newName = NULL) : menu_item::Selection(newName) {
#if HAVE_OLED
		basicTitle = "Gate outputs";
#endif
	}

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (!navigatedBackwardFrom) {
			soundEditor.currentValue = 0;
		}
		else {
			soundEditor.currentValue = soundEditor.currentSourceIndex;
		}
		menu_item::Selection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		if (soundEditor.currentValue == NUM_GATE_CHANNELS) {
			return &gateOffTimeMenu;
		}
		else {
			soundEditor.currentSourceIndex = soundEditor.currentValue;
#if HAVE_OLED
			gate::mode_title[8] = '1' + soundEditor.currentValue;
#endif

			// TODO: this needs to be a "UpdateOptions" method on gate::Mode
			gateModeMenu.updateOptions(soundEditor.currentValue);
			return &gateModeMenu;
		}
	}

	Sized<char const**> getOptions() override {
#if HAVE_OLED

		static char const* options[] = {"Gate output 1", "Gate output 2", "Gate output 3", "Gate output 4",
		                                "Minimum off-time"};
#else
		static char const* options[] = {"Out1", "Out2", "Out3", "Out4", "OFFT"};
#endif
		return {options, 5};
	}
};
} // namespace deluge::gui::menu_item::gate
