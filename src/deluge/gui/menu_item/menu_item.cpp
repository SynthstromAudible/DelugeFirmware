/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "menu_item.h"
#include "hid/display/numeric_driver.h"

using namespace deluge;

int MenuItem::checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {
	bool toReturn = isRelevant(sound, whichThing);
	return toReturn ? MENU_PERMISSION_YES : MENU_PERMISSION_NO;
}

void MenuItem::learnCC(MIDIDevice* fromDevice, int channel, int ccNumber, int value) {
	learnKnob(fromDevice, ccNumber, 0, channel);
}

#if HAVE_OLED

// This is virtual. Some classes with override it and generate some name on the fly.
// Supplied buffer size must be MENU_ITEM_TITLE_BUFFER_SIZE. Actual max num chars for OLED display is 14.
// May return pointer to that buffer, or to some other constant char string.
char const* MenuItem::getTitle() { //char* buffer) {
	return title;
}

void MenuItem::renderOLED() {
	OLED::drawScreenTitle(getTitle());
	drawPixelsForOled();
}

#else

void MenuItem::drawName() {
	numericDriver.setText(getName(), false, shouldDrawDotOnName());
}

#endif
