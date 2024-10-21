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
void MenuItem::drawItemsForOled(std::span<std::string_view> options, const int32_t selectedOption,
                                const int32_t offset) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	auto it = std::next(options.begin(), offset); // fast-forward to the first option visible
	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1 && o < options.size() - offset; o++) {
		int32_t yPixel = o * kTextSpacingY + baseY;

		image.drawString(options[o + offset], kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			image.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, options[o + offset], kTextSpacingX, OLED_MAIN_WIDTH_PIXELS,
			                                              yPixel, yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}

// renders the default sub menu item type ("  >")
void MenuItem::renderSubmenuItemTypeForOled(int32_t yPixel) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t startX = getSubmenuItemTypeRenderIconStart();

	image.drawGraphicMultiLine(deluge::hid::display::OLED::submenuArrowIcon, startX, yPixel, kSubmenuIconSpacingX);
}
