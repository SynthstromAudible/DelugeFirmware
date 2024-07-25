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
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include <string_view>

using namespace deluge;

MenuPermission MenuItem::checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
                                                       MultiRange** currentRange) {
	bool toReturn = isRelevant(modControllable, whichThing);
	return toReturn ? MenuPermission::YES : MenuPermission::NO;
}

void MenuItem::learnCC(MIDIDevice* fromDevice, int32_t channel, int32_t ccNumber, int32_t value) {
	learnKnob(fromDevice, ccNumber, 0, channel);
}

void MenuItem::renderOLED() {
	deluge::hid::display::OLED::main.drawScreenTitle(getTitle());
	deluge::hid::display::OLED::markChanged();
	drawPixelsForOled();
}

void MenuItem::drawName() {
	display->setText(getName(), false, shouldDrawDotOnName());
}

// A couple of our child classes call this - that's all
// This function now handles receiving a second array of strings to display at the end of a menu item
// (currently this is only used by submenu's to display ">" to indicate that you can enter the menu
// and also toggle menu's to display a "[x]" checkbox that you can toggle without entering the submenu)
void MenuItem::drawItemsForOled(std::span<std::string_view> options, const int32_t selectedOption, const int32_t offset,
                                bool renderTypes, std::span<std::string_view> types) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	auto it = std::next(options.begin(), offset); // fast-forward to the first option visible

	int32_t endX = OLED_MAIN_WIDTH_PIXELS; // default end X position for a menu item

	// if you are going to render type, we need to adjust the end position of the menu string
	// drawn before the type is drawn (so the strings don't overlap)
	if (renderTypes) {
		// fast-forward to the first type visible
		auto type = std::next(types.begin(), offset);

		// number of characters for type drawn ("  >" or "[ ]")
		int32_t typeLength = 3 + (kTextSpacingX * 3);

		// adjusting end position of menu item string
		endX -= (typeLength + kTextSpacingX);
	}

	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1 && o < options.size() - offset; o++) {
		int32_t yPixel = o * kTextSpacingY + baseY;

		// draw menu item string
		// if we're rendering type the menu item string will be cut off (so it doesn't overlap)
		// it will scroll below whenever you select that menu item
		image.drawString(options[o + offset], kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY, 0, endX);

		// if we're rendering type, we will render it after the end of the menu item string
		if (renderTypes) {
			image.drawString(types[o + offset], endX + kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY);
		}

		// if you've selected a menu item, invert the area to show that it is selected
		// and setup scrolling in case that menu item is too long to display fully
		if (o == selectedOption) {
			image.invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, options[o + offset], kTextSpacingX, endX, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}
