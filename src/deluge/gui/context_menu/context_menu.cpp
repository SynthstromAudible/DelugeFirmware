/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/context_menu.h"

#include "definitions_cxx.hpp"
#include "extern.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"

namespace deluge::gui {

bool ContextMenu::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool ContextMenu::setupAndCheckAvailability() {
	const auto [_options, numOptions] = getOptions();
	for (currentOption = 0; currentOption < numOptions; currentOption++) {
		if (isCurrentOptionAvailable()) {
			scrollPos = currentOption;
			return true;
		}
	}
	return false;
}

// No need for this - parent "UI" does it for us.
/*
bool ContextMenu::opened() {
    focusRegained();
    return true;
}
*/

void ContextMenu::focusRegained() {
	if (display->have7SEG()) {
		drawCurrentOption();
	}
}

void ContextMenu::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	const auto [options, numOptions] = getOptions();

	int32_t windowWidth = 100;
	int32_t windowHeight = 40;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	int32_t windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int32_t windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

	hid::display::OLED::clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1, image);

	hid::display::OLED::drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY, image);
	hid::display::OLED::drawHorizontalLine(windowMinY + 15, 22, OLED_MAIN_WIDTH_PIXELS - 30, &image[0]);
	hid::display::OLED::drawString(this->getTitle(), 22, windowMinY + 6, image[0], OLED_MAIN_WIDTH_PIXELS,
	                               kTextSpacingX, kTextSpacingY);

	int32_t textPixelY = windowMinY + 18;
	int32_t actualCurrentOption = currentOption;

	currentOption = scrollPos;
	int32_t i = 0;

	while (true) {
		if (currentOption >= numOptions) {
			break;
		}
		if (i >= 2) {
			break;
		}

		if (isCurrentOptionAvailable()) {
			deluge::hid::display::OLED::drawString(options[currentOption], 22, textPixelY, image[0],
			                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY, 0,
			                                       OLED_MAIN_WIDTH_PIXELS - 26);
			if (currentOption == actualCurrentOption) {
				deluge::hid::display::OLED::invertArea(22, OLED_MAIN_WIDTH_PIXELS - 44, textPixelY, textPixelY + 8,
				                                       &image[0]);
				deluge::hid::display::OLED::setupSideScroller(0, options[currentOption], 22,
				                                              OLED_MAIN_WIDTH_PIXELS - 26, textPixelY, textPixelY + 8,
				                                              kTextSpacingX, kTextSpacingY, true);
			}
			textPixelY += kTextSpacingY;
			i++;
		}
		currentOption++;
	}

	currentOption = actualCurrentOption;
}

void ContextMenu::selectEncoderAction(int8_t offset) {
	const auto [_options, numOptions] = getOptions();

	if (display->haveOLED()) {
		bool wasOnScrollPos = (currentOption == scrollPos);
		int32_t oldCurrentOption = currentOption;
		do {
			currentOption += offset;
			if (currentOption >= numOptions || currentOption < 0) {
				currentOption = oldCurrentOption;
				return;
			}
		} while (!isCurrentOptionAvailable());

		if (currentOption < scrollPos) {
			scrollPos = currentOption;
		}
		else if (offset >= 0 && !wasOnScrollPos) {
			scrollPos = oldCurrentOption;
		}
		renderUIsForOled();
	}
	else {
		do {
			if (offset >= 0) {
				currentOption++;
				if (currentOption >= numOptions) {
					currentOption -= numOptions;
				}
			}
			else {
				currentOption--;
				if (currentOption < 0) {
					currentOption += numOptions;
				}
			}

		} while (!isCurrentOptionAvailable());
		drawCurrentOption();
	}
}

ActionResult ContextMenu::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
getOut:
			display->setNextTransitionDirection(-1);
			close();
		}
	}

	else if (b == SELECT_ENC) {
probablyAcceptCurrentOption:
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			bool success = acceptCurrentOption();
			if (!success) {
				goto getOut;
			}
		}
	}

	else if (b == getAcceptButton()) {
		goto probablyAcceptCurrentOption;
	}

	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void ContextMenu::drawCurrentOption() {
	const auto [options, _size] = getOptions();
	if (display->have7SEG()) {
		indicator_leds::ledBlinkTimeout(0, true);
		display->setText(options[currentOption], false, 255, true);
	}
}

ActionResult ContextMenu::padAction(int32_t x, int32_t y, int32_t on) {
	if (on && !currentUIMode) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		display->setNextTransitionDirection(-1);
		close();
	}

	return ActionResult::DEALT_WITH;
}

void ContextMenuForSaving::focusRegained() {
	indicator_leds::setLedState(IndicatorLED::LOAD, false);
	indicator_leds::blinkLed(IndicatorLED::SAVE);
	return ContextMenu::focusRegained();
}

void ContextMenuForLoading::focusRegained() {
	indicator_leds::setLedState(IndicatorLED::SAVE, false);
	indicator_leds::blinkLed(IndicatorLED::LOAD);
	return ContextMenu::focusRegained();
}
} // namespace deluge::gui
