/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "value.h"
#include "gui/ui/ui.h"
#include "hid/display.h"

namespace menu_item {

void Value::beginSession(MenuItem* navigatedBackwardFrom) {
	if (display.type == DisplayType::OLED) {
		readCurrentValue();
	}
	else {
		readValueAgain();
	}
}

void Value::selectEncoderAction(int offset) {
	writeCurrentValue();

	// For MenuItems referring to an AutoParam (so UnpatchedParam and PatchedParam), ideally we wouldn't want to render the display here, because that'll happen soon anyway due to a setting of TIMER_DISPLAY_AUTOMATION.
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		drawValue(); // Probably not necessary either...
	}
}

void Value::readValueAgain() {
	readCurrentValue();
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}
} // namespace menu_item
