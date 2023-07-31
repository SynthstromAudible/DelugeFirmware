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
#include "gui/menu_item/selection/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "transpose.h"
#include "volts.h"

extern void setCvNumberForTitle(int m);
extern deluge::gui::menu_item::Submenu<2> cvSubmenu;

namespace deluge::gui::menu_item::cv {
class Selection final : public menu_item::Selection<2> {
public:
	using menu_item::Selection<2>::Selection;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (navigatedBackwardFrom == nullptr) {
			this->value_ = 0;
		}
		else {
			this->value_ = soundEditor.currentSourceIndex;
		}
		menu_item::Selection<2>::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		soundEditor.currentSourceIndex = this->value_;
#if HAVE_OLED
		cvSubmenu.title = getOptions().at(this->value_);
		setCvNumberForTitle(this->value_);
#endif
		return &cvSubmenu;
	}

	static_vector<string, capacity()> getOptions() override {
#if HAVE_OLED
		return {"CV output 1", "CV output 2"};
#else
		return {"Out1", "Out2"};
#endif
	}
};
} // namespace deluge::gui::menu_item::cv
