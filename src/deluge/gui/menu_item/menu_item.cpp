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
#include "hid/display.h"

MenuPermission MenuItem::checkPermissionToBeginSession(Sound* sound, int32_t whichThing, MultiRange** currentRange) {
	bool toReturn = isRelevant(sound, whichThing);
	return toReturn ? MenuPermission::YES : MenuPermission::NO;
}

void MenuItem::learnCC(MIDIDevice* fromDevice, int32_t channel, int32_t ccNumber, int32_t value) {
	learnKnob(fromDevice, ccNumber, 0, channel);
}

// This is virtual. Some classes with override it and generate some name on the fly.
// May return pointer to that buffer, or to some other constant char string.
char const* MenuItem::getTitle() {
	return basicTitle;
}

void MenuItem::renderOLED() {
	OLED::drawScreenTitle(getTitle());
	drawPixelsForOled();
}

// A couple of our child classes call this - that's all
void MenuItem::drawItemsForOled(char const** options, int32_t selectedOption) {

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1; o++) {
		if (!options[o]) {
			break;
		}

		int32_t yPixel = o * kTextSpacingY + baseY;

		OLED::drawString(options[o], kTextSpacingX, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o], kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
			                        kTextSpacingX, kTextSpacingY, true);
		}
	}
}

void MenuItem::drawName() {
	display.setText(getName(), false, shouldDrawDotOnName());
}
