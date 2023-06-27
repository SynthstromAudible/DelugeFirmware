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

#include "MenuItemValue.h"
#include "UI.h"

void MenuItemValue::beginSession(MenuItem* navigatedBackwardFrom) {
#if HAVE_OLED
	readCurrentValue();
#else
	readValueAgain();
#endif
}

void MenuItemValue::selectEncoderAction(int offset) {
	writeCurrentValue();

	// For MenuItems referring to an AutoParam (so MenuItemUnpatchedParam and MenuItemPatchedParam), ideally we wouldn't want to render the display here, because that'll happen soon anyway due to a setting of TIMER_DISPLAY_AUTOMATION.
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue(); // Probably not necessary either...
#endif
}

void MenuItemValue::readValueAgain() {
	readCurrentValue();
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue();
#endif
}
