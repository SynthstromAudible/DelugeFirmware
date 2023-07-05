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
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "util/cfunctions.h"
#include "string.h"
#include "processing/sound/sound.h"

extern void setOscillatorNumberForTitles(int);

namespace menu_item::submenu {

class ActualSource final : public SubmenuReferringToOneThing {
public:
	ActualSource(char const* newName = 0, MenuItem** newItems = 0, int newSourceIndex = 0)
	    : SubmenuReferringToOneThing(newName, newItems, newSourceIndex) {}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setOscillatorNumberForTitles(thingIndex);
		SubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
	}
#else
	void drawName() {
		if (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_FM) {
			char buffer[5];
			strcpy(buffer, "CAR");
			intToString(thingIndex + 1, buffer + 3);
			numericDriver.setText(buffer);
		}
		else {
			SubmenuReferringToOneThing::drawName();
		}
	}
#endif
};

} // namespace menu_item::submenu
