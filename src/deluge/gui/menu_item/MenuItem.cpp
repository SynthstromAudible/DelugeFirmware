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

#include "MenuItem.h"
#include "numericdriver.h"

#if HAVE_OLED
#include "oled.h"
#endif

MenuItem::MenuItem(char const* newName) {
	name = newName;
#if HAVE_OLED
	basicTitle = newName;
#endif
}

char const* MenuItem::getName() {
	return name;
}

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
	return basicTitle;
}

void MenuItem::renderOLED() {
	OLED::drawScreenTitle(getTitle());
	drawPixelsForOled();
}

// A couple of our child classes call this - that's all
void MenuItem::drawItemsForOled(char const** options, int selectedOption) {

	int baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int o = 0; o < OLED_HEIGHT_CHARS - 1; o++) {
		if (!options[o]) break;

		int yPixel = o * TEXT_SPACING_Y + baseY;

		OLED::drawString(options[o], TEXT_SPACING_X, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 TEXT_SPACING_X, TEXT_SPACING_Y);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o], TEXT_SPACING_X, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
			                        TEXT_SPACING_X, TEXT_SPACING_Y, true);
		}
	}
}

#else

void MenuItem::drawName() {
	numericDriver.setText(getName(), false, shouldDrawDotOnName());
}

#endif
