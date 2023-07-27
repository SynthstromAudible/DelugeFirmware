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
#include "hid/display.h"
#include "off_time.h"
#include "gui/ui/sound_editor.h"
#include "mode.h"

extern menu_item::gate::OffTime gateOffTimeMenu;
extern menu_item::gate::Mode gateModeMenu;

namespace menu_item::gate {
class Selection final : public menu_item::Selection {
public:
	Selection(char const* newName = NULL) : menu_item::Selection(newName) {
		basicTitle = "Gate outputs";
		static char const* options[] = {HAVE_OLED ? "Gate output 1" : "Out1",    // 1
		                                HAVE_OLED ? "Gate output 2" : "Out2",    // 2
		                                HAVE_OLED ? "Gate output 3" : "Out3",    // 3
		                                HAVE_OLED ? "Gate output 4" : "Out4",    // 4
		                                HAVE_OLED ? "Minimum off-time" : "OFFT", // Minimum time off
		                                NULL};
		basicOptions = options;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) {
			soundEditor.currentValue = 0;
		}
		else {
			soundEditor.currentValue = soundEditor.currentSourceIndex;
		}
		menu_item::Selection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() {
		if (soundEditor.currentValue == NUM_GATE_CHANNELS) {
			return &gateOffTimeMenu;
		}
		else {
			soundEditor.currentSourceIndex = soundEditor.currentValue;
			if (display.type == DisplayType::OLED) {
				gate::mode_title[8] = '1' + soundEditor.currentValue;
			}

			// TODO: this needs to be a "UpdateOptions" method on gate::Mode
			switch (soundEditor.currentValue) {
			case WHICH_GATE_OUTPUT_IS_CLOCK:
				mode_options[2] = "Clock";
				break;

			case WHICH_GATE_OUTPUT_IS_RUN:
				mode_options[2] = HAVE_OLED ? "\"Run\" signal" : "Run";
				break;

			default:
				mode_options[2] = NULL;
				break;
			}
			return &gateModeMenu;
		}
	}
};
} // namespace menu_item::gate
