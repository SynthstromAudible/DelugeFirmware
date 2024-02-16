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

#include "gui/views/snake_view.h"
#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/menus.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "mem_functions.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"

extern "C" {}

using namespace deluge;
using namespace gui;
// using namespace deluge::gui::menu_item;

SnakeView snakeView{};

// initialize variables
SnakeView::SnakeView() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			snakeGrid[xDisplay][yDisplay] = 0;
		}
	}
	snakeDirection = 1;
	snakeHead.xDisplay = 0;
	snakeHead.yDisplay = 0;
	snakeTail.xDisplay = 0;
	snakeTail.yDisplay = 0;
	rowTickSquarePrevious = kNoSelection;
	rowTickOffset = kNoSelection;
	snakeDied = false;
}

bool SnakeView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	focusRegained();

	return true;
}

void SnakeView::focusRegained() {
	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);

	currentSong->affectEntire = true;

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	setCentralLEDStates();
	setLedStates();

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void SnakeView::graphicsRoutine() {

	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	// Nothing to do here but clear since we don't render playhead
	memset(&tickSquares, 255, sizeof(tickSquares));
	memset(&colours, 255, sizeof(colours));
	PadLEDs::setTickSquares(tickSquares, colours);

	if (playbackHandler.isEitherClockActive()) {
		int32_t rowTickSquareNew = getSquareFromPos(currentSong->getLivePos());

		if (rowTickSquarePrevious == kNoSelection) {
			rowTickSquarePrevious = rowTickSquareNew;
		}

		if (rowTickOffset == kNoSelection) {
			rowTickOffset = 0;
		}
		else if (rowTickOffset == 0) {
			rowTickOffset = rowTickSquareNew - rowTickSquarePrevious;
		}
		else {
			rowTickOffset = 0;
			rowTickSquarePrevious = kNoSelection;
		}

		if (rowTickOffset > 0) {
			if (snakeDirection == 0) { // left
				if ((snakeHead.xDisplay - rowTickOffset) >= 0) {
					snakeHead.xDisplay = snakeHead.xDisplay - rowTickOffset;
					snakeDied = false;
				}
				else {
					snakeDied = true;
				}
			}
			else if (snakeDirection == 1) { // right
				if ((snakeHead.xDisplay + rowTickOffset) < kDisplayWidth) {
					snakeHead.xDisplay = snakeHead.xDisplay + rowTickOffset;
					snakeDied = false;
				}
				else {
					snakeDied = true;
				}
			}
			else if (snakeDirection == 2) { // up
				if ((snakeHead.yDisplay + rowTickOffset) < kDisplayHeight) {
					snakeHead.yDisplay = snakeHead.yDisplay + rowTickOffset;
					snakeDied = false;
				}
				else {
					snakeDied = true;
				}
			}
			else if (snakeDirection == 3) { // down
				if ((snakeHead.yDisplay - rowTickOffset) >= 0) {
					snakeHead.yDisplay = snakeHead.yDisplay - rowTickOffset;
					snakeDied = false;
				}
				else {
					snakeDied = true;
				}
			}

			uiNeedsRendering(this);
		}
	}
}

ActionResult SnakeView::timerCallback() {
	return ActionResult::DEALT_WITH;
}

bool SnakeView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                               uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	PadLEDs::renderingLock = true;

	// erase current image as it will be refreshed
	memset(image, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth) * 3);

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];
		int32_t imageWidth = kDisplayWidth + kSideBarWidth;

		renderRow(&image[0][0] + (yDisplay * imageWidth), occupancyMaskOfRow, yDisplay);
	}

	PadLEDs::renderingLock = false;

	return true;
}

void SnakeView::renderRow(RGB* image, uint8_t occupancyMask[], int32_t yDisplay) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		RGB& pixel = image[xDisplay];

		if (snakeDied) {
			pixel = {
			    .r = 255,
			    .g = 0,
			    .b = 0,
			};
		}
		else if ((snakeHead.xDisplay == xDisplay) && (snakeHead.yDisplay == yDisplay)) {
			pixel = {
			    .r = 130,
			    .g = 120,
			    .b = 130,
			};
		}
		occupancyMask[xDisplay] = 64;
	}
}

bool SnakeView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	int32_t xDisplay = kDisplayWidth;
	int32_t yDisplay = 3;

	// left arrow
	{
		RGB& pixel = image[yDisplay][xDisplay];
		pixel = {
		    .r = 130,
		    .g = 120,
		    .b = 130,
		};
		occupancyMask[yDisplay][xDisplay] = 64;
	}

	// right arrow
	{
		RGB& pixel = image[yDisplay][xDisplay + 1];
		pixel = {
		    .r = 130,
		    .g = 120,
		    .b = 130,
		};
		occupancyMask[yDisplay][xDisplay + 1] = 64;
	}

	// up arrow
	{
		RGB& pixel = image[yDisplay + 1][xDisplay + 1];
		pixel = {
		    .r = 130,
		    .g = 120,
		    .b = 130,
		};
		occupancyMask[yDisplay + 1][xDisplay + 1] = 64;
	}

	// down arrow
	{
		RGB& pixel = image[yDisplay - 1][xDisplay + 1];
		pixel = {
		    .r = 130,
		    .g = 120,
		    .b = 130,
		};
		occupancyMask[yDisplay - 1][xDisplay - 1] = 64;
	}

	return true;
}

// render performance view display on opening
void SnakeView::renderViewDisplay() {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

		yPos = yPos + 12;

		deluge::hid::display::OLED::drawStringCentred("Snake View", yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();
	}
	else {
		display->setScrollingText("Snake View");
	}
}

void SnakeView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	renderViewDisplay();
}

void SnakeView::redrawNumericDisplay() {
	renderViewDisplay();
}

void SnakeView::setLedStates() {
	view.setLedStates();
	view.setModLedStates();
}

void SnakeView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);
}

ActionResult SnakeView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == KEYBOARD) {
		if (on) {
			changeRootUI(&sessionView);
		}
	}

	// disable button presses for Vertical encoder
	else if (b == Y_ENC) {
		goto doNothing;
	}

	else {
notDealtWith:
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}

doNothing:
	return ActionResult::DEALT_WITH;
}

ActionResult SnakeView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	// if pad was pressed in main deluge grid (not sidebar)
	// change snake direction
	if (xDisplay >= kDisplayWidth) {
		if (on) {
			if (xDisplay == kDisplayWidth && yDisplay == 3) {
				snakeDirection = 0; // left
			}			
			else if (xDisplay == (kDisplayWidth + 1) && yDisplay == 3) {
				snakeDirection = 1; // right
			}
			else if (xDisplay == (kDisplayWidth + 1) && yDisplay == 4) {
				snakeDirection = 2; // up
			}
			else if (xDisplay == (kDisplayWidth + 1) && yDisplay == 2) {
				snakeDirection = 3; // down
			}
		}
	}
	return ActionResult::DEALT_WITH;
}

// Used to edit a pad's value in editing mode
void SnakeView::selectEncoderAction(int8_t offset) {
	return;
}

ActionResult SnakeView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult SnakeView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

// why do I need this? (code won't compile without it)
uint32_t SnakeView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

// why do I need this? (code won't compile without it)
uint32_t SnakeView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

// updates the display if the mod encoder has just updated the same parameter currently being held / last held
// if no param is currently being held, it will reset the display to just show "Performance View"
void SnakeView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (getCurrentUI() == this) { // This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}

// used to reset stutter if it's already active
void SnakeView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	UI::modEncoderButtonAction(whichModEncoder, on);
}

void SnakeView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}
