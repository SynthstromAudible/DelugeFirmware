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
#include "dsp/master_compressor/master_compressor.h"
#include "extern.h"
#include "gui/colour.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge;
using namespace gui;
//using namespace deluge::gui::menu_item;

SnakeView snakeView{};

//initialize variables
SnakeView::SnakeView() {
	return;
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

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();

	setCentralLEDStates();
	setLedStates();

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void SnakeView::graphicsRoutine() {
	return;
}

ActionResult SnakeView::timerCallback() {
	return ActionResult::DEALT_WITH;
}

bool SnakeView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
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

	performActualRender(whichRows, &image[0][0][0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);

	PadLEDs::renderingLock = false;

	return true;
}

//render performance mode
void SnakeView::performActualRender(uint32_t whichRows, uint8_t* image,
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                    uint32_t xZoom, int32_t renderWidth, int32_t imageWidth, bool drawUndefinedArea) {

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

		renderRow(image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
	}
}

//this function started off as a copy of the renderRow function from the NoteRow class - I replaced "notes" with "nodes"
//it worked for the most part, but there was bugs so I removed the buggy code and inserted my alternative rendering method
//which always works. hoping to bring back the other code once I've worked out the bugs.
void SnakeView::renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {
	return;
}

bool SnakeView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	return true;
}

//render performance view display on opening
void SnakeView::renderViewDisplay() {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

		yPos = yPos + 12;

		deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_SNAKE_VIEW), yPos,
		                                              deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();
	}
	else {
		display->setScrollingText(l10n::get(l10n::String::STRING_FOR_SNAKE_VIEW));
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

	//disable button presses for Vertical encoder
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
	//if pad was pressed in main deluge grid (not sidebar)
	//	if (xDisplay < kDisplayWidth) {
	//	}
	return ActionResult::DEALT_WITH;
}

//Used to edit a pad's value in editing mode
void SnakeView::selectEncoderAction(int8_t offset) {
	return;
}

ActionResult SnakeView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult SnakeView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

//why do I need this? (code won't compile without it)
uint32_t SnakeView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

//why do I need this? (code won't compile without it)
uint32_t SnakeView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

//updates the display if the mod encoder has just updated the same parameter currently being held / last held
//if no param is currently being held, it will reset the display to just show "Performance View"
void SnakeView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}

//used to reset stutter if it's already active
void SnakeView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	UI::modEncoderButtonAction(whichModEncoder, on);
}

void SnakeView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}
