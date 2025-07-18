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
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "storage/flash_storage.h"

namespace deluge::gui {

bool ContextMenu::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool ContextMenu::setupAndCheckAvailability() {
	const size_t numOptions = getOptions().size();
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
	indicator_leds::blinkLed(IndicatorLED::BACK);
	if (display->have7SEG()) {
		drawCurrentOption();
	}
}

void ContextMenu::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	const auto options = getOptions();

	int32_t windowWidth = 100;
	int32_t windowHeight = 40;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	int32_t windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int32_t windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

	canvas.clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1);

	canvas.drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
	canvas.drawHorizontalLine(windowMinY + 15, 22, OLED_MAIN_WIDTH_PIXELS - 30);
	canvas.drawString(this->getTitle(), 22, windowMinY + 6, kTextSpacingX, kTextSpacingY);

	int32_t textPixelY = windowMinY + 18;
	int32_t actualCurrentOption = currentOption;

	currentOption = scrollPos;
	int32_t i = 0;

	while (true) {
		if (currentOption >= options.size()) {
			break;
		}
		if (i >= 2) {
			break;
		}

		if (isCurrentOptionAvailable()) {
			int32_t invertStartX = 22;
			int32_t textPixelX = invertStartX + 1;
			if (FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION) {
				textPixelX += kTextSpacingX;
			}
			canvas.drawString(options[currentOption], textPixelX, textPixelY, kTextSpacingX, kTextSpacingY, 0,
			                  OLED_MAIN_WIDTH_PIXELS - 27);
			if (currentOption == actualCurrentOption) {
				canvas.invertLeftEdgeForMenuHighlighting(invertStartX, OLED_MAIN_WIDTH_PIXELS - 44, textPixelY,
				                                         textPixelY + 8);
				deluge::hid::display::OLED::setupSideScroller(0, options[currentOption], textPixelX,
				                                              OLED_MAIN_WIDTH_PIXELS - 27, textPixelY, textPixelY + 8,
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
	const size_t numOptions = getOptions().size();

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

const uint32_t buttonAndPadActionUIModes[] = {UI_MODE_STEM_EXPORT, UI_MODE_CLIP_PRESSED_IN_SONG_VIEW,
                                              UI_MODE_MIDI_LEARN, 0};

ActionResult ContextMenu::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == BACK) {
		if (on && isUIModeWithinRange(buttonAndPadActionUIModes)) {
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
		if (on && isUIModeWithinRange(buttonAndPadActionUIModes)) {
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
	const auto options = getOptions();
	if (display->have7SEG()) {
		indicator_leds::ledBlinkTimeout(0, true);
		display->setText(options[currentOption], false, 255, true);
	}
}

ActionResult ContextMenu::padAction(int32_t x, int32_t y, int32_t on) {
	if (on && isUIModeWithinRange(buttonAndPadActionUIModes)) {
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
	ContextMenu::focusRegained();
}

void ContextMenuForLoading::focusRegained() {
	indicator_leds::setLedState(IndicatorLED::SAVE, false);
	indicator_leds::blinkLed(IndicatorLED::LOAD);
	ContextMenu::focusRegained();
}
} // namespace deluge::gui
