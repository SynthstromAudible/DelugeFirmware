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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "gui/menu_item/submenu.h"
#include "volts.h"
#include "transpose.h"

extern void setCvNumberForTitle(int m);
extern menu_item::Submenu cvSubmenu;

namespace menu_item::cv {
#if HAVE_OLED
static char const* cvOutputChannel[] = {"CV output 1", "CV output 2", NULL};
#else
static char const* cvOutputChannel[] = {"Out1", "Out2", NULL};
#endif

class Selection final : public menu_item::Selection {
public:
	Selection(char const* newName = NULL) : menu_item::Selection(newName) {
#if HAVE_OLED
		basicTitle = "CV outputs";
#endif
		basicOptions = cvOutputChannel;
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
		soundEditor.currentSourceIndex = soundEditor.currentValue;
#if HAVE_OLED
		cvSubmenu.basicTitle = cvOutputChannel[soundEditor.currentValue];
		setCvNumberForTitle(soundEditor.currentValue);
#endif
		return &cvSubmenu;
	}
};
} // namespace menu_item::cv
