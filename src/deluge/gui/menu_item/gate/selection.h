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
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "mode.h"
#include "off_time.h"

extern deluge::gui::menu_item::gate::OffTime gateOffTimeMenu;
extern deluge::gui::menu_item::gate::Mode gateModeMenu;
namespace deluge::gui::menu_item::gate {

class Selection final : public menu_item::Selection {
public:
	using menu_item::Selection::Selection;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (navigatedBackwardFrom == nullptr) {
			this->setValue(0);
		}
		else {
			this->setValue(soundEditor.currentSourceIndex);
		}
		menu_item::Selection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		if (this->getValue() == NUM_GATE_CHANNELS) {
			return &gateOffTimeMenu;
		}
		soundEditor.currentSourceIndex = this->getValue();

		if (display->haveOLED()) {
			gateModeMenu.format(this->getValue());
		}

		gateModeMenu.updateOptions(this->getValue());
		return &gateModeMenu;
	}

	std::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		static auto out1 = l10n::getView(STRING_FOR_GATE_OUTPUT_1);
		static auto out2 = l10n::getView(STRING_FOR_GATE_OUTPUT_2);
		static auto out3 = l10n::getView(STRING_FOR_GATE_OUTPUT_3);
		static auto out4 = l10n::getView(STRING_FOR_GATE_OUTPUT_4);

		return {
		    out1,                                       //<
		    out2,                                       //<
		    out3,                                       //<
		    out4,                                       //<
		    l10n::getView(STRING_FOR_MINIMUM_OFF_TIME), //<
		};
	}
};
} // namespace deluge::gui::menu_item::gate
