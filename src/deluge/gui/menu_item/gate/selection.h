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
#include "mode.h"
#include "off_time.h"
#include "util/container/static_vector.hpp"

extern deluge::gui::menu_item::gate::OffTime gateOffTimeMenu;
extern deluge::gui::menu_item::gate::Mode gateModeMenu;
namespace deluge::gui::menu_item::gate {

class Selection final : public menu_item::Selection<5> {
public:
	using menu_item::Selection<capacity()>::Selection;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (navigatedBackwardFrom == nullptr) {
			this->setValue(0);
		}
		else {
			this->setValue(soundEditor.currentSourceIndex);
		}
		menu_item::Selection<capacity()>::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		if (this->getValue() == NUM_GATE_CHANNELS) {
			return &gateOffTimeMenu;
		}
		soundEditor.currentSourceIndex = this->getValue();
#if HAVE_OLED
		gate::mode_title[8] = '1' + this->getValue();
#endif

		// TODO: this needs to be a "UpdateOptions" method on gate::Mode
		gateModeMenu.updateOptions(this->getValue());
		return &gateModeMenu;
	}

	static_vector<string, capacity()> getOptions() override {
#if HAVE_OLED

		return {"Gate output 1", "Gate output 2", "Gate output 3", "Gate output 4", "Minimum off-time"};
#else
		return {"Out1", "Out2", "Out3", "Out4", "OFFT"};
#endif
	}
};
} // namespace deluge::gui::menu_item::gate
