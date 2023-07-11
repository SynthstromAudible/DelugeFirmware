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

#include "hid/display/numeric_driver.h"
#include "util/functions.h"
#include "hid/led/indicator_leds.h"
#include "extern.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

namespace deluge::gui {

ContextMenu::ContextMenu() {
#if HAVE_OLED
	oledShowsUIUnderneath = true;
#endif
}

bool ContextMenu::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool ContextMenu::setupAndCheckAvailability() {
	const auto [_options, numOptions] = getOptions();
	for (currentOption = 0; currentOption < numOptions; currentOption++) {
		if (isCurrentOptionAvailable()) {
#if HAVE_OLED
			scrollPos = currentOption;
#endif
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
#if !HAVE_OLED
	drawCurrentOption();
#endif
}

#if HAVE_OLED
void ContextMenu::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	const auto [options, numOptions] = getOptions();

	int windowWidth = 100;
	int windowHeight = 40;

	int windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	int windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;

	int windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

	OLED::clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1, image);

	OLED::drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY, image);
	OLED::drawHorizontalLine(windowMinY + 15, 22, OLED_MAIN_WIDTH_PIXELS - 30, &image[0]);
	OLED::drawString(this->getTitle(), 22, windowMinY + 6, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SPACING_Y);

	int textPixelY = windowMinY + 18;
	int actualCurrentOption = currentOption;

	currentOption = scrollPos;
	int i = 0;

	while (true) {
		if (currentOption >= numOptions) {
			break;
		}
		if (i >= 2) {
			break;
		}

		if (isCurrentOptionAvailable()) {
			OLED::drawString(options[currentOption], 22, textPixelY, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
			                 TEXT_SPACING_Y, 0, OLED_MAIN_WIDTH_PIXELS - 22);
			if (currentOption == actualCurrentOption) {
				OLED::invertArea(22, OLED_MAIN_WIDTH_PIXELS - 44, textPixelY, textPixelY + 8, &image[0]);
				OLED::setupSideScroller(0, options[currentOption], 22, OLED_MAIN_WIDTH_PIXELS - 22, textPixelY,
				                        textPixelY + 8, TEXT_SPACING_X, TEXT_SPACING_Y, true);
			}
			textPixelY += TEXT_SPACING_Y;
			i++;
		}
		currentOption++;
	}

	currentOption = actualCurrentOption;
}
#endif

void ContextMenu::selectEncoderAction(int8_t offset) {
	const auto [_options, numOptions] = getOptions();

#if HAVE_OLED
	bool wasOnScrollPos = (currentOption == scrollPos);
	int oldCurrentOption = currentOption;
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
#else

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
#endif
}

int ContextMenu::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
getOut:
			numericDriver.setNextTransitionDirection(-1);
			close();
		}
	}

	else if (b == SELECT_ENC) {
probablyAcceptCurrentOption:
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void ContextMenu::drawCurrentOption() {
	const auto [options, _size] = getOptions();

#if HAVE_OLED

#else
	indicator_leds::ledBlinkTimeout(0, true);
	numericDriver.setText(options[currentOption], false, 255, true);
#endif
}

int ContextMenu::padAction(int x, int y, int on) {
	if (on && !currentUIMode) {
		if (sdRoutineLock) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		numericDriver.setNextTransitionDirection(-1);
		close();
	}

	return ACTION_RESULT_DEALT_WITH;
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
