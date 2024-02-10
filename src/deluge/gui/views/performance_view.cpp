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

#include "gui/views/performance_view.h"
#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/menus.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_follow.h"
#include "mem_functions.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"

extern "C" {}

namespace params = deluge::modulation::params;
using deluge::modulation::params::Kind;
using deluge::modulation::params::kNoParamID;
using namespace deluge;
using namespace gui;

#define PERFORM_DEFAULTS_FOLDER "PERFORMANCE_VIEW"
#define PERFORM_DEFAULTS_XML "default.XML"
#define PERFORM_DEFAULTS_TAG "defaults"
#define PERFORM_DEFAULTS_FXVALUES_TAG "defaultFXValues"
#define PERFORM_DEFAULTS_PARAM_TAG "param"
#define PERFORM_DEFAULTS_NO_PARAM "none"
#define PERFORM_DEFAULTS_HOLD_TAG "hold"
#define PERFORM_DEFAULTS_HOLD_STATUS_TAG "status"
#define PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG "resetValue"
#define PERFORM_DEFAULTS_ROW_TAG "row"
#define PERFORM_DEFAULTS_ON "On"
#define PERFORM_DEFAULTS_OFF "Off"

using namespace deluge::modulation::params;

// list of parameters available for assignment to FX columns in performance view
constexpr std::array<ParamsForPerformance, kNumParamsForPerformance> songParamsForPerformance = {{
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_LPF_FREQ, 8, 7, colours::red, colours::red.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_LPF_RES, 8, 6, colours::red, colours::red.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_HPF_FREQ, 9, 7, colours::pastel::orange, colours::pastel::orangeTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_HPF_RES, 9, 6, colours::pastel::orange, colours::pastel::orangeTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_BASS, 10, 6, colours::pastel::yellow, colours::pastel::yellow.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_TREBLE, 11, 6, colours::pastel::yellow, colours::pastel::yellow.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_REVERB_SEND_AMOUNT, 13, 3, colours::pastel::green,
     colours::pastel::green.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_DELAY_AMOUNT, 14, 3, colours::pastel::blue, colours::pastel::blue.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_DELAY_RATE, 14, 0, colours::pastel::blue, colours::pastel::blue.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_RATE, 12, 7, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_DEPTH, 12, 6, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_FEEDBACK, 12, 5, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_OFFSET, 12, 4, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_SAMPLE_RATE_REDUCTION, 6, 5, colours::magenta, colours::magenta.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_BITCRUSHING, 6, 6, colours::magenta, colours::magenta.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_STUTTER_RATE, 5, 7, colours::blue, colours::blue.forTail()},
}};

constexpr std::array<ParamsForPerformance, kDisplayWidth> defaultLayoutForPerformance = {{
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_LPF_FREQ, 8, 7, colours::red, colours::red.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_LPF_RES, 8, 6, colours::red, colours::red.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_HPF_FREQ, 9, 7, colours::pastel::orange, colours::pastel::orangeTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_HPF_RES, 9, 6, colours::pastel::orange, colours::pastel::orangeTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_BASS, 10, 6, colours::pastel::yellow, colours::pastel::yellow.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_TREBLE, 11, 6, colours::pastel::yellow, colours::pastel::yellow.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_REVERB_SEND_AMOUNT, 13, 3, colours::pastel::green,
     colours::pastel::green.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_DELAY_AMOUNT, 14, 3, colours::pastel::blue, colours::pastel::blue.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_DELAY_RATE, 14, 0, colours::pastel::blue, colours::pastel::blue.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_RATE, 12, 7, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_DEPTH, 12, 6, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_FEEDBACK, 12, 5, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_MOD_FX_OFFSET, 12, 4, colours::pastel::pink, colours::pastel::pinkTail},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_SAMPLE_RATE_REDUCTION, 6, 5, colours::magenta, colours::magenta.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_BITCRUSHING, 6, 6, colours::magenta, colours::magenta.forTail()},
    {Kind::UNPATCHED_GLOBAL, UNPATCHED_STUTTER_RATE, 5, 7, colours::blue, colours::blue.forTail()},
}};

// mapping shortcuts to paramKind
constexpr Kind paramKindShortcutsForPerformanceView[kDisplayWidth][kDisplayHeight] = {
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_GLOBAL,
     Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL,
     Kind::UNPATCHED_GLOBAL},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL,
     Kind::UNPATCHED_GLOBAL},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL, Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_GLOBAL,
     Kind::UNPATCHED_GLOBAL, Kind::UNPATCHED_GLOBAL},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
    {Kind::UNPATCHED_GLOBAL, Kind::NONE, Kind::NONE, Kind::UNPATCHED_GLOBAL, Kind::NONE, Kind::NONE, Kind::NONE,
     Kind::NONE},
    {Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE, Kind::NONE},
};

// mapping shortcuts to paramID
constexpr uint32_t paramIDShortcutsForPerformanceView[kDisplayWidth][kDisplayHeight] = {
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_STUTTER_RATE},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_SAMPLE_RATE_REDUCTION, UNPATCHED_BITCRUSHING,
     kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_LPF_RES, UNPATCHED_LPF_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_HPF_RES, UNPATCHED_HPF_FREQ},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_BASS, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_TREBLE, kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, UNPATCHED_MOD_FX_OFFSET, UNPATCHED_MOD_FX_FEEDBACK,
     UNPATCHED_MOD_FX_DEPTH, UNPATCHED_MOD_FX_RATE},
    {kNoParamID, kNoParamID, kNoParamID, UNPATCHED_REVERB_SEND_AMOUNT, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
    {UNPATCHED_DELAY_RATE, kNoParamID, kNoParamID, UNPATCHED_DELAY_AMOUNT, kNoParamID, kNoParamID, kNoParamID,
     kNoParamID},
    {kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID, kNoParamID},
};

PerformanceView performanceView{};

// initialize variables
PerformanceView::PerformanceView() {
	successfullyReadDefaultsFromFile = false;

	anyChangesToSave = false;

	defaultEditingMode = false;

	layoutBank = 0;
	morphMode = false;
	backupMorphALayout = false;
	backupMorphBLayout = false;
	morphPosition = 0;

	onFXDisplay = false;
	onMorphDisplay = false;

	performanceLayoutBackedUp = false;

	justExitedSoundEditor = false;

	gridModeActive = false;
	timeGridModePress = 0;

	initPadPress(firstPadPress);
	initPadPress(lastPadPress);

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		initFXPress(fxPress[xDisplay]);
		initFXPress(backupFXPress[xDisplay]);
		initFXPress(backupXMLDefaultFXPress[xDisplay]);
		initFXPress(morphAFXPress[xDisplay]);
		initFXPress(morphBFXPress[xDisplay]);

		initLayout(layoutForPerformance[xDisplay]);
		initLayout(backupXMLDefaultLayoutForPerformance[xDisplay]);
		initLayout(morphALayoutForPerformance[xDisplay]);
		initLayout(morphBLayoutForPerformance[xDisplay]);

		initDefaultFXValues(xDisplay);
	}

	tempFilePath.clear();
}

void PerformanceView::initPadPress(PadPress& padPress) {
	padPress.isActive = false;
	padPress.xDisplay = kNoSelection;
	padPress.yDisplay = kNoSelection;
	padPress.paramKind = params::Kind::NONE;
	padPress.paramID = kNoSelection;
}

void PerformanceView::initFXPress(FXColumnPress& columnPress) {
	columnPress.previousKnobPosition = kNoSelection;
	columnPress.currentKnobPosition = kNoSelection;
	columnPress.yDisplay = kNoSelection;
	columnPress.timeLastPadPress = 0;
	columnPress.padPressHeld = false;
}

void PerformanceView::initLayout(ParamsForPerformance& layout) {
	layout.paramKind = params::Kind::NONE;
	layout.paramID = kNoSelection;
	layout.xDisplay = kNoSelection;
	layout.yDisplay = kNoSelection;
	layout.rowColour[0] = 0;
	layout.rowColour[1] = 0;
	layout.rowColour[2] = 0;
	layout.rowTailColour[0] = 0;
	layout.rowTailColour[1] = 0;
	layout.rowTailColour[2] = 0;
}

void PerformanceView::initDefaultFXValues(int32_t xDisplay) {
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t defaultFXValue = calculateKnobPosForSinglePadPress(xDisplay, yDisplay);
		defaultFXValues[xDisplay][yDisplay] = defaultFXValue;
		backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValue;
		morphAFXValues[xDisplay][yDisplay] = defaultFXValue;
		morphBFXValues[xDisplay][yDisplay] = defaultFXValue;
	}
}

bool PerformanceView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	focusRegained();

	return true;
}

void PerformanceView::focusRegained() {
	currentSong->onPerformanceView = true;
	currentSong->affectEntire = true;

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	if (!successfullyReadDefaultsFromFile) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		readDefaultsFromFile(modelStack);
		actionLogger.deleteAllLogs();
	}

	setLedStates();

	updateLayoutChangeStatus();

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void PerformanceView::graphicsRoutine() {
	static int counter = 0;
	if (currentUIMode == UI_MODE_NONE) {
		int32_t modKnobMode = -1;
		bool editingComp = false;
		if (view.activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;
				editingComp = view.activeModControllableModelStack.modControllable->isEditingComp();
			}
		}
		if (modKnobMode == 4 && editingComp) { // upper
			counter = (counter + 1) % 5;
			if (counter == 0) {
				uint8_t gr = currentSong->globalEffectable.compressor.gainReduction;

				indicator_leds::setMeterLevel(1, gr); // Gain Reduction LED
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	// Nothing to do here but clear since we don't render playhead
	memset(&tickSquares, 255, sizeof(tickSquares));
	memset(&colours, 255, sizeof(colours));
	PadLEDs::setTickSquares(tickSquares, colours);
}

ActionResult PerformanceView::timerCallback() {
	if (currentSong->lastClipInstanceEnteredStartPos == -1) {
		sessionView.timerCallback();
	}
	else {
		arrangerView.timerCallback();
	}
	return ActionResult::DEALT_WITH;
}

bool PerformanceView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                     uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	PadLEDs::renderingLock = true;

	// erase current image as it will be refreshed
	memset(image, 0, sizeof(RGB) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	// erase current occupancy mask as it will be refreshed
	memset(occupancyMask, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	// render performance view
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];
		int32_t imageWidth = kDisplayWidth + kSideBarWidth;

		renderRow(&image[0][0] + (yDisplay * imageWidth), occupancyMaskOfRow, yDisplay);
	}

	PadLEDs::renderingLock = false;

	return true;
}

/// render every column, one row at a time
void PerformanceView::renderRow(RGB* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		RGB& pixel = image[xDisplay];

		if (editingParam) {
			// if you're in param editing mode, highlight shortcuts for performance view params
			// if param has been assigned to an FX column, highlight it white, otherwise highlight it grey
			if (isPadShortcut(xDisplay, yDisplay)) {
				if (isParamAssignedToFXColumn(paramKindShortcutsForPerformanceView[xDisplay][yDisplay],
				                              paramIDShortcutsForPerformanceView[xDisplay][yDisplay])) {
					pixel = {
					    .r = 130,
					    .g = 120,
					    .b = 130,
					};
				}
				else {
					pixel = colours::grey;
				}
			}
			// if you're in param editing mode and pressing a shortcut pad, highlight the columns
			// that the param is assigned to the colour of that FX column
			if (firstPadPress.isActive) {
				if ((layoutForPerformance[xDisplay].paramKind == firstPadPress.paramKind)
				    && (layoutForPerformance[xDisplay].paramID == firstPadPress.paramID)) {
					pixel = layoutForPerformance[xDisplay].rowTailColour;
				}
			}
		}
		else {
			// elsewhere in performance view, if an FX column has not been assigned a param,
			// highlight the column grey
			if (layoutForPerformance[xDisplay].paramID == kNoSelection) {
				pixel = colours::grey;
			}
			else {
				// if you're currently pressing an FX column, highlight it a bright colour
				if ((fxPress[xDisplay].currentKnobPosition != kNoSelection)
				    && (fxPress[xDisplay].padPressHeld == false)) {
					pixel = layoutForPerformance[xDisplay].rowColour;
				}
				// if you're not currently pressing an FX column, highlight it a dimmer colour
				else {
					pixel = layoutForPerformance[xDisplay].rowTailColour;
				}

				// if you're currently pressing an FX column, highlight the pad you're pressing white
				if ((fxPress[xDisplay].currentKnobPosition == defaultFXValues[xDisplay][yDisplay])
				    && (fxPress[xDisplay].yDisplay == yDisplay)) {
					pixel = {
					    .r = 130,
					    .g = 120,
					    .b = 130,
					};
				}
			}
		}

		occupancyMask[xDisplay] = 64;
	}
}

/// check if a param has been assigned to any of the FX columns
bool PerformanceView::isParamAssignedToFXColumn(params::Kind paramKind, int32_t paramID) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if ((layoutForPerformance[xDisplay].paramKind == paramKind)
		    && (layoutForPerformance[xDisplay].paramID == paramID)) {
			return true;
		}
	}
	return false;
}

/// depending on if you entered performance view from arranger or song:
/// renders the sidebar from song view (grid mode or row mode)
/// renders the sidebar from arranger view
bool PerformanceView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (!occupancyMask) {
		return true;
	}

	if (currentSong->lastClipInstanceEnteredStartPos == -1) {
		sessionView.renderSidebar(whichRows, image, occupancyMask);
	}
	else {
		arrangerView.renderSidebar(whichRows, image, occupancyMask);
	}

	return true;
}

/// render performance view display on opening
void PerformanceView::renderViewDisplay() {
	if (getCurrentUI() == this) {
		if (defaultEditingMode) {
			if (display->haveOLED()) {
				deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

				// render "Performance View" at top of OLED screen
				deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW), yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

				yPos = yPos + 12;

				char const* editingModeType;

				// render "Param" or "Value" in the middle of the OLED screen
				if (editingParam) {
					editingModeType = l10n::get(l10n::String::STRING_FOR_PERFORM_EDIT_PARAM);
				}
				else {
					editingModeType = l10n::get(l10n::String::STRING_FOR_PERFORM_EDIT_VALUE);
				}

				deluge::hid::display::OLED::drawStringCentred(editingModeType, yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

				yPos = yPos + 12;

				// render "Editing Mode" at the bottom of the OLED screen
				deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_EDITOR), yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

				deluge::hid::display::OLED::sendMainImage();
			}
			else {
				display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORM_EDITOR));
			}
		}
		else {
			if (display->haveOLED()) {
				deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

				yPos = yPos + 12;

				// Render "Performance View" in the middle of the OLED screen
				deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW), yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

				deluge::hid::display::OLED::sendMainImage();
			}
			else {
				display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORM_VIEW));
			}
		}
		onFXDisplay = false;
		onMorphDisplay = false;
	}
}

/// Render Parameter Name and Value set when using Performance Pads
void PerformanceView::renderFXDisplay(params::Kind paramKind, int32_t paramID, int32_t knobPos) {
	if (getCurrentUI() == this) {
		if (editingParam) {
			// display parameter name
			char parameterName[30];
			strncpy(parameterName, getParamDisplayName(paramKind, paramID), 29);
			if (display->haveOLED()) {
				deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
				yPos = yPos + 12;

				deluge::hid::display::OLED::drawStringCentred(parameterName, yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

				deluge::hid::display::OLED::sendMainImage();
			}
			else {
				display->setScrollingText(parameterName);
			}
		}
		else {
			if (display->haveOLED()) {
				deluge::hid::display::OLED::clearMainImage();

				// display parameter name
				char parameterName[30];
				strncpy(parameterName, getParamDisplayName(paramKind, paramID), 29);

#if OLED_MAIN_HEIGHT_PIXELS == 64
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
				int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
				deluge::hid::display::OLED::drawStringCentred(parameterName, yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

				// display parameter value
				yPos = yPos + 24;

				if (params::isParamQuantizedStutter(paramKind, paramID)) {
					char const* buffer;
					if (knobPos < -39) { // 4ths stutter: no leds turned on
						buffer = "4ths";
					}
					else if (knobPos < -14) { // 8ths stutter: 1 led turned on
						buffer = "8ths";
					}
					else if (knobPos < 14) { // 16ths stutter: 2 leds turned on
						buffer = "16ths";
					}
					else if (knobPos < 39) { // 32nds stutter: 3 leds turned on
						buffer = "32nds";
					}
					else { // 64ths stutter: all 4 leds turned on
						buffer = "64ths";
					}
					deluge::hid::display::OLED::drawStringCentred(buffer, yPos,
					                                              deluge::hid::display::OLED::oledMainImage[0],
					                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
				}
				else {
					char buffer[5];
					intToString(knobPos, buffer);
					deluge::hid::display::OLED::drawStringCentred(buffer, yPos,
					                                              deluge::hid::display::OLED::oledMainImage[0],
					                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
				}

				deluge::hid::display::OLED::sendMainImage();
			}
			// 7Seg Display
			else {
				if (params::isParamQuantizedStutter(paramKind, paramID)) {
					char const* buffer;
					if (knobPos < -39) { // 4ths stutter: no leds turned on
						buffer = "4ths";
					}
					else if (knobPos < -14) { // 8ths stutter: 1 led turned on
						buffer = "8ths";
					}
					else if (knobPos < 14) { // 16ths stutter: 2 leds turned on
						buffer = "16th";
					}
					else if (knobPos < 39) { // 32nds stutter: 3 leds turned on
						buffer = "32nd";
					}
					else { // 64ths stutter: all 4 leds turned on
						buffer = "64th";
					}
					display->displayPopup(buffer, 3, true);
				}
				else {
					char buffer[5];
					intToString(knobPos, buffer);
					display->displayPopup(buffer, 3, true);
				}
			}
		}
		onFXDisplay = true;
		onMorphDisplay = false;
	}
}

void PerformanceView::renderMorphDisplay() {
	if (getCurrentUI() == this) {
		if (display->haveOLED()) {
			deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

			// render "Morph Mode" at the top of OLED screen
			deluge::hid::display::OLED::drawStringCentred("Morph Mode", yPos,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

			yPos = yPos + 15;

			if (isMorphingPossible()) {
				// render "Morph Value" in second row of OLED screen above the bar
				char buffer[10];
				intToString(calculateMorphPositionForDisplay(), buffer);
				deluge::hid::display::OLED::drawStringCentred(buffer, yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
			}
			else {
				deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORM_CANT_MORPH),
				                                              yPos, deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
			}

			yPos = 35;

			int32_t variant = currentSong->performanceMorphLayoutAVariant;
			if (variant != kNoSelection) {
				char buffer[5];
				if (variant == 0) {
					buffer[0] = 'D';
					buffer[1] = 0;
				}
				else {
					intToString(variant, buffer);
				}
				deluge::hid::display::OLED::drawString(buffer, 10, yPos, deluge::hid::display::OLED::oledMainImage[0],
				                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
			}

			variant = currentSong->performanceMorphLayoutBVariant;
			if (variant != kNoSelection) {
				char buffer[5];
				if (variant == 0) {
					buffer[0] = 'D';
					buffer[1] = 0;
				}
				else {
					intToString(variant, buffer);
				}

				deluge::hid::display::OLED::drawString(buffer, OLED_MAIN_WIDTH_PIXELS - 15, yPos,
				                                       deluge::hid::display::OLED::oledMainImage[0],
				                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
			}

			drawMorphBar(yPos);

			deluge::hid::display::OLED::sendMainImage();
		}
		else {
			if (isMorphingPossible()) {
				char buffer[10];
				intToString(calculateMorphPositionForDisplay(), buffer);
				display->setText(buffer);
			}
			else {
				display->setText(l10n::get(l10n::String::STRING_FOR_PERFORM_CANT_MORPH));
			}
		}
		onMorphDisplay = true;
		onFXDisplay = false;
	}
}

int32_t PerformanceView::calculateMorphPositionForDisplay() {
	float knobPosFloat = static_cast<float>(morphPosition);
	float knobPosOffsetFloat = static_cast<float>(kKnobPosOffset);
	float maxKnobPosFloat = static_cast<float>(kMaxKnobPos);
	float maxMenuValueFloat = static_cast<float>(kMaxMenuValue * 2);
	float maxMenuRelativeValueFloat = static_cast<float>(kMaxMenuRelativeValue * 2);
	float valueForDisplayFloat;

	// calculate parameter value for display by converting 0 - 128 range to -50 to +50 range
	valueForDisplayFloat = ((knobPosFloat / maxKnobPosFloat) * maxMenuValueFloat) - maxMenuRelativeValueFloat;

	return static_cast<int32_t>(std::round(valueForDisplayFloat));
}

void PerformanceView::drawMorphBar(int32_t yTop) {
	int32_t marginL = 20;
	int32_t marginR = marginL;

	int32_t height = 7;

	int32_t leftMost = marginL;
	int32_t rightMost = OLED_MAIN_WIDTH_PIXELS - marginR - 1;

	int32_t y = OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_VISIBLE_HEIGHT * 0.78;

	int32_t endLineHalfHeight = 8;

	int32_t minValue = -64;
	int32_t maxValue = +64;
	uint32_t range = maxValue - minValue;
	int32_t positionForDisplay = isMorphingPossible() ? morphPosition - kKnobPosOffset : 0;
	float posFractional = (float)(positionForDisplay - minValue) / range;
	float zeroPosFractional = (float)(-minValue) / range;

	int32_t width = rightMost - leftMost;
	int32_t posHorizontal = (int32_t)(posFractional * width + 0.5);
	int32_t zeroPosHorizontal = (int32_t)(zeroPosFractional * width);

	if (posHorizontal <= zeroPosHorizontal) {
		int32_t xMin = leftMost + posHorizontal;
		deluge::hid::display::OLED::invertArea(xMin, zeroPosHorizontal - posHorizontal + 1, yTop, yTop + height,
		                                       deluge::hid::display::OLED::oledMainImage);
	}
	else {
		int32_t xMin = leftMost + zeroPosHorizontal;
		deluge::hid::display::OLED::invertArea(xMin, posHorizontal - zeroPosHorizontal, yTop, yTop + height,
		                                       deluge::hid::display::OLED::oledMainImage);
	}
	deluge::hid::display::OLED::drawRectangle(leftMost, yTop, rightMost, yTop + height,
	                                          deluge::hid::display::OLED::oledMainImage);
}

void PerformanceView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	if (morphMode) {
		renderMorphDisplay();
	}
	else {
		renderViewDisplay();
	}
	sessionView.renderOLED(image);
}

void PerformanceView::redrawNumericDisplay() {
	if (morphMode) {
		renderMorphDisplay();
	}
	else {
		renderViewDisplay();
	}
	sessionView.redrawNumericDisplay();
}

void PerformanceView::setLedStates() {
	setCentralLEDStates();
	view.setLedStates();
	view.setModLedStates();
}

void PerformanceView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);

	// if you're in the default editing mode (editing param values, or param layout)
	// blink keyboard button to show that you're in editing mode
	// if there are changes to save while in editing mode, blink save button
	// if you're not in editing mode, light up keyboard button to show that you're
	// in performance view but not editing mode. also turn off save button led
	// as we only blink save button when we're in editing mode
	// if we're not in editing mode, also make sure to refresh the morph led states
	if (defaultEditingMode) {
		indicator_leds::blinkLed(IndicatorLED::KEYBOARD);
		if (anyChangesToSave) {
			indicator_leds::blinkLed(IndicatorLED::SAVE);
		}
		else {
			indicator_leds::setLedState(IndicatorLED::SAVE, false);
		}
	}
	else {
		indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
		indicator_leds::setLedState(IndicatorLED::SAVE, false);
		setMorphLEDStates();
	}

	if (morphMode) {
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, true);
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
	}
	else if (layoutBank == 1) {
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, true);
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
	else if (layoutBank == 2) {
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
	}
	else {
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

ActionResult PerformanceView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	using namespace deluge::hid::button;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	// enter/exit Performance View when used on its own (e.g. not holding load/save)
	// enter/cycle/exit editing modes when used while holding shift button
	if (b == KEYBOARD && currentUIMode == UI_MODE_NONE) {
		handleKeyboardButtonAction(on, modelStack);
	}

	// enter "Perform FX" soundEditor menu
	else if ((b == SELECT_ENC) && !Buttons::isShiftButtonPressed()) {
		handleSelectEncoderButtonAction(on);
	}

	// show current root note and scale name
	else if (b == Y_ENC) {
		handleVerticalEncoderButtonAction(on);
	}

	// enter exit Horizontal Encoder Button Press UI Mode
	else if (b == X_ENC) {
		handleHorizontalEncoderButtonAction(on);
	}

	// clear and reset held params
	else if (b == BACK && isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		handleBackAndHorizontalEncoderButtonComboAction(on, modelStack);
	}

	// enter or exit morph mode
	else if (((b == SCALE_MODE) && Buttons::isButtonPressed(deluge::hid::button::CROSS_SCREEN_EDIT))
	         || ((b == CROSS_SCREEN_EDIT) && Buttons::isButtonPressed(deluge::hid::button::SCALE_MODE))) {
		handleScaleAndCrossButtonComboAction(on);
	}

	// select alternate layout bank and display/blink current variant loaded
	else if ((b == SCALE_MODE) && !morphMode) {
		handleScaleButtonAction(on, currentSong->performanceLayoutVariant);
	}

	// select alternate layout bank and display/blink current variant loaded
	else if ((b == CROSS_SCREEN_EDIT) && !morphMode) {
		handleCrossButtonAction(on, currentSong->performanceLayoutVariant);
	}

	// save default performance view layout
	else if (b == KEYBOARD && !morphMode && isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
		handleKeyboardAndSaveButtonComboAction(on);
	}

	// save alternate performance view layout
	else if (b == SYNTH && !morphMode && isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
		handleSynthAndSaveButtonComboAction(on);
	}

	// save alternate performance view layout
	else if (b == KIT && !morphMode && isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
		handleKitAndSaveButtonComboAction(on);
	}

	// save alternate performance view layout
	else if (b == MIDI && !morphMode && isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
		handleMidiAndSaveButtonComboAction(on);
	}

	// save alternate performance view layout
	else if (b == CV && !morphMode && isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
		handleCVAndSaveButtonComboAction(on);
	}

	// load performance view layout
	else if (b == KEYBOARD && !morphMode && isUIModeActive(UI_MODE_HOLDING_LOAD_BUTTON)) {
		handleKeyboardAndLoadButtonComboAction(on, modelStack);
	}

	// load alternate performance view layout
	else if (b == SYNTH && !morphMode && isUIModeActive(UI_MODE_HOLDING_LOAD_BUTTON)) {
		handleSynthAndLoadButtonComboAction(on, modelStack);
	}

	// load alternate performance view layout
	else if (b == KIT && !morphMode && isUIModeActive(UI_MODE_HOLDING_LOAD_BUTTON)) {
		handleKitAndLoadButtonComboAction(on, modelStack);
	}

	// load alternate performance view layout
	else if (b == MIDI && !morphMode && isUIModeActive(UI_MODE_HOLDING_LOAD_BUTTON)) {
		handleMidiAndLoadButtonComboAction(on, modelStack);
	}

	// load alternate performance view layout
	else if (b == CV && !morphMode && isUIModeActive(UI_MODE_HOLDING_LOAD_BUTTON)) {
		handleCVAndLoadButtonComboAction(on, modelStack);
	}

	else if (b == SYNTH && morphMode && !Buttons::isButtonPressed(deluge::hid::button::CV)) {
		handleSynthMorphButtonAction(on, modelStack);
	}

	else if (b == CV && morphMode && !Buttons::isButtonPressed(deluge::hid::button::SYNTH)) {
		handleCVMorphButtonAction(on, modelStack);
	}

	else {
		ActionResult buttonActionResult;
		buttonActionResult = TimelineView::buttonAction(b, on, inCardRoutine);

		// release stutter if you press play - stutter needs to be turned on after playback is running
		// re-render grid, display if undoing/redoing an action (e.g. you previously loaded layout)
		// update change status if undoing/redoing an action
		if (on && (b == PLAY || b == BACK)) {
			if (b == PLAY) {
				releaseStutter(modelStack);
			}
			else if (b == BACK) {
				initPadPress(lastPadPress);
				updateLayoutChangeStatus();
				if (onFXDisplay) {
					renderViewDisplay();
				}
			}
			uiNeedsRendering(this);
		}
		return buttonActionResult;
	}
	return ActionResult::DEALT_WITH;
}

/// called by button action if b == KEYBOARD
void PerformanceView::handleKeyboardButtonAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		if (Buttons::isShiftButtonPressed()) {
			if (defaultEditingMode && editingParam) {
				defaultEditingMode = false;
				editingParam = false;
				indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
			}
			else {
				if (morphMode) {
					exitMorphMode();
				}
				if (!defaultEditingMode) {
					resetPerformanceView(modelStack);
					indicator_leds::blinkLed(IndicatorLED::KEYBOARD);
				}
				else {
					editingParam = true;
				}
				defaultEditingMode = true;
			}
			updateLayoutChangeStatus();
			renderViewDisplay();
			uiNeedsRendering(this);
		}
		else {
			gridModeActive = false;
			releaseStutter(modelStack);
			if (currentSong->lastClipInstanceEnteredStartPos != -1) {
				changeRootUI(&arrangerView);
			}
			else {
				changeRootUI(&sessionView);
			}
		}
	}
}

/// called by button action if b == Y_ENC
void PerformanceView::handleVerticalEncoderButtonAction(bool on) {
	if (on) {
		currentSong->displayCurrentRootNoteAndScaleName();
	}
}

/// called by button action if b == X_ENC
void PerformanceView::handleHorizontalEncoderButtonAction(bool on) {
	if (on) {
		enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
	}
	else {
		if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
			exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
	}
}

/// called by button action if b == back and UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON
void PerformanceView::handleBackAndHorizontalEncoderButtonComboAction(bool on,
                                                                      ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		resetPerformanceView(modelStack);
	}
}

/// called by button action if scale and cross-screen buttons are both held
/// enters or exits morph mode
void PerformanceView::handleScaleAndCrossButtonComboAction(bool on) {
	if (on) {
		if (morphMode) {
			exitMorphMode();
		}
		else {
			enterMorphMode();
		}
	}
}

/// called by button action if you're not in morph mode and you press the scale button
/// selects layout bank 1
void PerformanceView::handleScaleButtonAction(bool on, int32_t variant) {
	if (on) {
		layoutBank = 1;
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, true);
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		if (variant == 1) {
			indicator_leds::blinkLed(IndicatorLED::SYNTH);
		}
		else if (variant == 2) {
			indicator_leds::blinkLed(IndicatorLED::KIT);
		}
		else if (variant == 3) {
			indicator_leds::blinkLed(IndicatorLED::MIDI);
		}
		else if (variant == 4) {
			indicator_leds::blinkLed(IndicatorLED::CV);
		}
		displayLayoutVariant(variant);
	}
	else {
		indicator_leds::setLedState(IndicatorLED::SYNTH, false);
		indicator_leds::setLedState(IndicatorLED::KIT, false);
		indicator_leds::setLedState(IndicatorLED::MIDI, false);
		indicator_leds::setLedState(IndicatorLED::CV, false);
	}
}

/// called by button action if you're not in morph mode and you press the cross screen button
/// select layout bank 2
void PerformanceView::handleCrossButtonAction(bool on, int32_t variant) {
	if (on) {
		layoutBank = 2;
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		if (variant == 5) {
			indicator_leds::blinkLed(IndicatorLED::SYNTH);
		}
		else if (variant == 6) {
			indicator_leds::blinkLed(IndicatorLED::KIT);
		}
		else if (variant == 7) {
			indicator_leds::blinkLed(IndicatorLED::MIDI);
		}
		else if (variant == 8) {
			indicator_leds::blinkLed(IndicatorLED::CV);
		}
		displayLayoutVariant(variant);
	}
	else {
		indicator_leds::setLedState(IndicatorLED::SYNTH, false);
		indicator_leds::setLedState(IndicatorLED::KIT, false);
		indicator_leds::setLedState(IndicatorLED::MIDI, false);
		indicator_leds::setLedState(IndicatorLED::CV, false);
	}
}

/// called by button action if keyboard button is pressed while UI_MODE_HOLDING_SAVE_BUTTON
/// saves default layout, unselects bank and displays saved variant popup
void PerformanceView::handleKeyboardAndSaveButtonComboAction(bool on) {
	if (on) {
		layoutBank = 0;
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		currentSong->performanceLayoutVariant = 0;
		savePerformanceViewLayout();
		displayLayoutVariant(currentSong->performanceLayoutVariant);
		exitUIMode(UI_MODE_HOLDING_SAVE_BUTTON); // to prevent going into saveUI
	}
}

/// called by button combo functions below to save bank variant based on current bank selection
void PerformanceView::handleSavingBankVariantSelection(int32_t bank1Variant, int32_t bank2Variant) {
	if (layoutBank == 1) {
		currentSong->performanceLayoutVariant = bank1Variant;
	}
	else if (layoutBank == 2) {
		currentSong->performanceLayoutVariant = bank2Variant;
	}
	if (currentSong->performanceLayoutVariant == bank1Variant
	    || currentSong->performanceLayoutVariant == bank2Variant) {
		savePerformanceViewLayout();
		displayLayoutVariant(currentSong->performanceLayoutVariant);
	}
	exitUIMode(UI_MODE_HOLDING_SAVE_BUTTON); // to prevent going into saveUI
}

/// called by button action if synth button is pressed while UI_MODE_HOLDING_SAVE_BUTTON
/// saves layout 1 or 5 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleSynthAndSaveButtonComboAction(bool on) {
	if (on) {
		handleSavingBankVariantSelection(1, 5);
	}
}

/// called by button action if kit button is pressed while UI_MODE_HOLDING_SAVE_BUTTON
/// saves layout 2 or 6 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleKitAndSaveButtonComboAction(bool on) {
	if (on) {
		handleSavingBankVariantSelection(2, 6);
	}
}

/// called by button action if midi button is pressed while UI_MODE_HOLDING_SAVE_BUTTON
/// saves layout 3 or 7 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleMidiAndSaveButtonComboAction(bool on) {
	if (on) {
		handleSavingBankVariantSelection(3, 7);
	}
}

/// called by button action if CV button is pressed while UI_MODE_HOLDING_SAVE_BUTTON
/// saves layout 4 or 8 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleCVAndSaveButtonComboAction(bool on) {
	if (on) {
		handleSavingBankVariantSelection(4, 8);
	}
}

/// called by button action if keyboard button is pressed while UI_MODE_HOLDING_LOAD_BUTTON
/// loads default layout, unselects bank and displays loaded variant popup
void PerformanceView::handleKeyboardAndLoadButtonComboAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		if (currentSong->performanceLayoutVariant != 0) {
			successfullyReadDefaultsFromFile = false;
		}
		layoutBank = 0;
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		currentSong->performanceLayoutVariant = 0;
		loadPerformanceViewLayout(modelStack);
		renderViewDisplay();
		displayLayoutVariant(currentSong->performanceLayoutVariant);
		exitUIMode(UI_MODE_HOLDING_LOAD_BUTTON); // to prevent going into loadUI
	}
}

/// called by button combo functions below to load bank variant based on current bank selection
void PerformanceView::handleLoadingBankVariantSelection(int32_t bank1Variant, int32_t bank2Variant,
                                                        ModelStackWithThreeMainThings* modelStack) {
	if (layoutBank == 1) {
		if (currentSong->performanceLayoutVariant != bank1Variant) {
			successfullyReadDefaultsFromFile = false;
		}
		currentSong->performanceLayoutVariant = bank1Variant;
	}
	else if (layoutBank == 2) {
		if (currentSong->performanceLayoutVariant != bank2Variant) {
			successfullyReadDefaultsFromFile = false;
		}
		currentSong->performanceLayoutVariant = bank2Variant;
	}
	if (currentSong->performanceLayoutVariant == bank1Variant
	    || currentSong->performanceLayoutVariant == bank2Variant) {
		loadPerformanceViewLayout(modelStack);
		renderViewDisplay();
		displayLayoutVariant(currentSong->performanceLayoutVariant);
	}
	exitUIMode(UI_MODE_HOLDING_LOAD_BUTTON); // to prevent going into loadUI
}

/// called by button action if synth button is pressed while UI_MODE_HOLDING_LOAD_BUTTON
/// loads layout 1 or 5 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleSynthAndLoadButtonComboAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		handleLoadingBankVariantSelection(1, 5, modelStack);
	}
}

/// called by button action if kit button is pressed while UI_MODE_HOLDING_LOAD_BUTTON
/// loads layout 2 or 6 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleKitAndLoadButtonComboAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		handleLoadingBankVariantSelection(2, 6, modelStack);
	}
}

/// called by button action if midi button is pressed while UI_MODE_HOLDING_LOAD_BUTTON
/// loads layout 3 or 7 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleMidiAndLoadButtonComboAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		handleLoadingBankVariantSelection(3, 7, modelStack);
	}
}

/// called by button action if CV button is pressed while UI_MODE_HOLDING_LOAD_BUTTON
/// loads layout 4 or 8 if bank 1 or 2 is selected and displays saved variant popup
void PerformanceView::handleCVAndLoadButtonComboAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		handleLoadingBankVariantSelection(4, 8, modelStack);
	}
}

/// called by button action if b == Synth while you're in Morph Mode and CV button isn't also pressed
void PerformanceView::handleSynthMorphButtonAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		loadMorphALayout(modelStack);
		if (display->have7SEG()) {
			displayLayoutVariant(currentSong->performanceMorphLayoutAVariant);
		}
		backupMorphALayout = true;
	}
	else {
		backupMorphALayout = false;
		if (!onMorphDisplay) {
			renderMorphDisplay();
		}
	}
}

/// called by button action if b == CV while you're in Morph Mode and Synth button isn't also pressed
void PerformanceView::handleCVMorphButtonAction(bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		loadMorphBLayout(modelStack);
		if (display->have7SEG()) {
			displayLayoutVariant(currentSong->performanceMorphLayoutBVariant);
		}
		backupMorphBLayout = true;
	}
	else {
		backupMorphBLayout = false;
		if (!onMorphDisplay) {
			renderMorphDisplay();
		}
	}
}

/// called by button action if b == SELECT_ENC and shift button is not also pressed
void PerformanceView::handleSelectEncoderButtonAction(bool on) {
	if (on) {
		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return;
		}

		display->setNextTransitionDirection(1);
		soundEditor.setup();
		openUI(&soundEditor);
	}
}

ActionResult PerformanceView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	if (!justExitedSoundEditor) {
		// if pad was pressed in main deluge grid (not sidebar)
		if (xDisplay < kDisplayWidth) {
			if (on) {
				// if it's a shortcut press, enter soundEditor menu for that parameter
				if (Buttons::isShiftButtonPressed()) {
					return soundEditor.potentialShortcutPadAction(xDisplay, yDisplay, on);
				}
			}
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			// if not in param editor (so, regular performance view or value editor)
			if (!editingParam) {
				bool ignorePadAction =
				    defaultEditingMode && lastPadPress.isActive && (lastPadPress.xDisplay != xDisplay);
				if (ignorePadAction || (layoutForPerformance[xDisplay].paramID == kNoSelection)) {
					return ActionResult::DEALT_WITH;
				}
				normalPadAction(modelStack, xDisplay, yDisplay, on);
			}
			// editing mode & editing parameter FX assignments
			else {
				paramEditorPadAction(modelStack, xDisplay, yDisplay, on);
			}
			uiNeedsRendering(this); // re-render pads
		}
		else if (xDisplay >= kDisplayWidth) {
			// if in arranger view
			if (currentSong->lastClipInstanceEnteredStartPos != -1) {
				// pressing the first column in sidebar to trigger sections / clips
				if (xDisplay == kDisplayWidth) {
					arrangerView.handleStatusPadAction(yDisplay, on, this);
				}
				// pressing the second column in sidebar to audition / edit instrument
				else {
					arrangerView.handleAuditionPadAction(yDisplay, on, this);
					// when you let go of audition pad action, you need to reset led states
					if (!on) {
						setCentralLEDStates();
						if (morphMode) {
							setKnobIndicatorLevels();
							view.setModLedStates();
						}
					}
				}
			}
			// if in session view
			else {
				// if in row mode
				if (!gridModeActive) {
					sessionView.padAction(xDisplay, yDisplay, on);
				}
				// if in grid mode
				else {
					// if you're in grid song view and you pressed / release a pad in the section launcher column
					if (xDisplay == kDisplayWidth) {
						sessionView.gridHandlePads(xDisplay, yDisplay, on);
					}
					else {
						// if you're using grid song view and you pressed / released a pad in the grid mode launcher
						// column
						if (xDisplay > kDisplayWidth) {
							// pressing the pink mode pad
							if (yDisplay == 0) {
								// if you released the pink pad and it was held for longer than hold time
								// switch back to session view (this happens if you enter performance view with a
								// long press from grid mode - it just peeks performance view)
								if (!on && ((AudioEngine::audioSampleTimer - timeGridModePress) >= kHoldTime)) {
									gridModeActive = false;
									changeRootUI(&sessionView);
								}
							}
							// if you pressed the green or blue mode pads, go back to grid view and change mode
							else if ((yDisplay == 7) || (yDisplay == 6)) {
								gridModeActive = false;
								changeRootUI(&sessionView);
								sessionView.gridHandlePads(xDisplay, yDisplay, on);
							}
						}
					}
				}
			}
		}
	}
	else if (!on) {
		justExitedSoundEditor = false;
	}
	return ActionResult::DEALT_WITH;
}

/// process pad actions in the normal performance view or value editor
void PerformanceView::normalPadAction(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay, int32_t yDisplay,
                                      int32_t on) {
	// obtain Kind, ParamID corresponding to the column pressed on performance grid
	Kind lastSelectedParamKind = layoutForPerformance[xDisplay].paramKind; // kind;
	int32_t lastSelectedParamID = layoutForPerformance[xDisplay].paramID;

	// pressing a pad
	if (on) {
		// no need to pad press action if you've already processed it previously and pad was held
		if (fxPress[xDisplay].yDisplay != yDisplay) {
			backupPerformanceLayout();
			// check if there a previously held press for this parameter in another column and disable it
			// also transfer the previous value for that held pad to this new pad column press
			for (int32_t i = 0; i < kDisplayWidth; i++) {
				if (i != xDisplay) {
					if ((layoutForPerformance[i].paramKind == lastSelectedParamKind)
					    && (layoutForPerformance[i].paramID == lastSelectedParamID)) {
						fxPress[xDisplay].previousKnobPosition = fxPress[i].previousKnobPosition;
						initFXPress(fxPress[i]);
						logPerformanceViewPress(i, false);
					}
				}
			}
			padPressAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, yDisplay,
			               !defaultEditingMode);
		}
	}
	// releasing a pad
	else {
		// if releasing a pad with "held" status shortly after being given that status
		// or releasing a pad that was not in "held" status but was a longer press and release
		if ((params::isParamStutter(lastSelectedParamKind, lastSelectedParamID) && lastPadPress.isActive
		     && lastPadPress.yDisplay == yDisplay)
		    || (fxPress[xDisplay].padPressHeld
		        && ((AudioEngine::audioSampleTimer - fxPress[xDisplay].timeLastPadPress) < kHoldTime))
		    || ((fxPress[xDisplay].previousKnobPosition != kNoSelection) && (fxPress[xDisplay].yDisplay == yDisplay)
		        && ((AudioEngine::audioSampleTimer - fxPress[xDisplay].timeLastPadPress) >= kHoldTime))) {

			padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, !defaultEditingMode);
		}
		// if releasing a pad that was quickly pressed, give it held status
		else if (!params::isParamStutter(lastSelectedParamKind, lastSelectedParamID)
		         && (fxPress[xDisplay].previousKnobPosition != kNoSelection) && (fxPress[xDisplay].yDisplay == yDisplay)
		         && ((AudioEngine::audioSampleTimer - fxPress[xDisplay].timeLastPadPress) < kHoldTime)) {
			fxPress[xDisplay].padPressHeld = true;
			// no need to keep track of lastPadPress in morph mode when a pad is held
			if (morphMode) {
				initPadPress(lastPadPress);
			}
		}
		// no saving of logs in performance view editing mode
		if (!defaultEditingMode) {
			logPerformanceViewPress(xDisplay);
		}
		updateLayoutChangeStatus();
		if (backupMorphALayout || backupMorphBLayout) {
			backupPerformanceLayout(true);
			setMorphLEDStates();
		}
	}

	// if you're in editing mode and not editing a param, pressing an FX column will open soundEditor menu
	// if a parameter has been assigned to that FX column
	if (defaultEditingMode && on) {
		int32_t lastSelectedParamShortcutX = layoutForPerformance[lastPadPress.xDisplay].xDisplay;
		int32_t lastSelectedParamShortcutY = layoutForPerformance[lastPadPress.xDisplay].yDisplay;

		// if you're not already in soundEditor, enter soundEditor
		// or if you're already in soundEditor, check if you're in the right menu
		if ((getCurrentUI() != &soundEditor)
		    || ((getCurrentUI() == &soundEditor)
		        && (soundEditor.getCurrentMenuItem()
		            != paramShortcutsForSongView[lastSelectedParamShortcutX][lastSelectedParamShortcutY]))) {
			soundEditor.potentialShortcutPadAction(layoutForPerformance[xDisplay].xDisplay,
			                                       layoutForPerformance[xDisplay].yDisplay, on);
		}
		// otherwise no need to do anything as you're already displaying the menu for the parameter
	}
}

void PerformanceView::padPressAction(ModelStackWithThreeMainThings* modelStack, params::Kind paramKind, int32_t paramID,
                                     int32_t xDisplay, int32_t yDisplay, bool renderDisplay) {
	if (setParameterValue(modelStack, paramKind, paramID, xDisplay, defaultFXValues[xDisplay][yDisplay],
	                      renderDisplay)) {
		// if pressing a new pad in a column, reset held status
		fxPress[xDisplay].padPressHeld = false;

		// save row yDisplay of current pad press in column xDisplay
		fxPress[xDisplay].yDisplay = yDisplay;

		// save time of current pad press in column xDisplay
		fxPress[xDisplay].timeLastPadPress = AudioEngine::audioSampleTimer;

		// update current knob position
		fxPress[xDisplay].currentKnobPosition = defaultFXValues[xDisplay][yDisplay];

		// save xDisplay, yDisplay, paramKind and paramID currently being edited
		lastPadPress.isActive = true;
		lastPadPress.xDisplay = xDisplay;
		lastPadPress.yDisplay = yDisplay;
		lastPadPress.paramKind = paramKind;
		lastPadPress.paramID = paramID;
	}
}

void PerformanceView::padReleaseAction(ModelStackWithThreeMainThings* modelStack, params::Kind paramKind,
                                       int32_t paramID, int32_t xDisplay, bool renderDisplay) {
	if (setParameterValue(modelStack, paramKind, paramID, xDisplay, fxPress[xDisplay].previousKnobPosition,
	                      renderDisplay)) {
		initFXPress(fxPress[xDisplay]);
		initPadPress(lastPadPress);
	}
}

/// process pad actions in the param editor
void PerformanceView::paramEditorPadAction(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay,
                                           int32_t yDisplay, int32_t on) {
	// pressing a pad
	if (on) {
		// if you haven't yet pressed and are holding a param shortcut pad on the param overview
		if (!firstPadPress.isActive) {
			if (isPadShortcut(xDisplay, yDisplay)) {
				firstPadPress.isActive = true;
				firstPadPress.paramKind = paramKindShortcutsForPerformanceView[xDisplay][yDisplay];
				firstPadPress.paramID = paramIDShortcutsForPerformanceView[xDisplay][yDisplay];
				firstPadPress.xDisplay = xDisplay;
				firstPadPress.yDisplay = yDisplay;
				renderFXDisplay(firstPadPress.paramKind, firstPadPress.paramID);
			}
		}
		// if you are holding a param shortcut pad and are now pressing a pad in an FX column
		else {
			// if the FX column you are pressing is currently assigned to a different param or no param
			if ((layoutForPerformance[xDisplay].paramKind != firstPadPress.paramKind)
			    || (layoutForPerformance[xDisplay].paramID != firstPadPress.paramID)
			    || (layoutForPerformance[xDisplay].xDisplay != firstPadPress.xDisplay)
			    || (layoutForPerformance[xDisplay].yDisplay != firstPadPress.yDisplay)) {

				// remove any existing holds from the FX column before assigning a new param
				resetFXColumn(modelStack, xDisplay);

				// assign new param to the FX column
				layoutForPerformance[xDisplay].paramKind = firstPadPress.paramKind;
				layoutForPerformance[xDisplay].paramID = firstPadPress.paramID;
				layoutForPerformance[xDisplay].xDisplay = firstPadPress.xDisplay;
				layoutForPerformance[xDisplay].yDisplay = firstPadPress.yDisplay;

				// assign new colour to the FX column based on the new param assigned
				for (int32_t i = 0; i < kNumParamsForPerformance; i++) {
					if ((songParamsForPerformance[i].paramKind == firstPadPress.paramKind)
					    && (songParamsForPerformance[i].paramID == firstPadPress.paramID)) {
						layoutForPerformance[xDisplay].rowColour = songParamsForPerformance[i].rowColour;
						layoutForPerformance[xDisplay].rowTailColour = songParamsForPerformance[i].rowTailColour;
						break;
					}
				}
			}
			// if you have already assigned the same param to the FX column, pressing the column will remove it
			else {
				// remove any existing holds from the FX column before clearing param from column
				resetFXColumn(modelStack, xDisplay);

				// remove param from FX column
				initLayout(layoutForPerformance[xDisplay]);
			}
			updateLayoutChangeStatus();
		}
	}
	// releasing a pad
	else {
		if ((firstPadPress.xDisplay == xDisplay) && (firstPadPress.yDisplay == yDisplay)) {
			initPadPress(firstPadPress);
			renderViewDisplay();
		}
	}
}

/// check if pad press corresponds to a shortcut pad on the grid
bool PerformanceView::isPadShortcut(int32_t xDisplay, int32_t yDisplay) {
	if ((paramKindShortcutsForPerformanceView[xDisplay][yDisplay] != params::Kind::NONE)
	    && (paramIDShortcutsForPerformanceView[xDisplay][yDisplay] != kNoParamID)) {
		return true;
	}
	return false;
}

/// backup performance layout so changes can be undone / redone later
void PerformanceView::backupPerformanceLayout(bool onlyMorph) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if (successfullyReadDefaultsFromFile) {
			if (!onlyMorph) {
				memcpy(&backupFXPress[xDisplay], &fxPress[xDisplay], sizeof(FXColumnPress));
				performanceLayoutBackedUp = true;
			}
			else {
				if (backupMorphALayout) {
					memcpy(&morphAFXPress[xDisplay], &fxPress[xDisplay], sizeof(FXColumnPress));
				}
				if (backupMorphBLayout) {
					memcpy(&morphBFXPress[xDisplay], &fxPress[xDisplay], sizeof(FXColumnPress));
				}
			}
		}
	}
}

/// used in conjunction with backupPerformanceLayout to log changes
/// while in Performance View so that you can undo/redo them afters
void PerformanceView::logPerformanceViewPress(int32_t xDisplay, bool closeAction) {
	if (anyChangesToLog()) {
		actionLogger.recordPerformanceViewPress(backupFXPress, fxPress, xDisplay);
		if (closeAction) {
			actionLogger.closeAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
		}
	}
}

/// check if there are any changes that needed to be logged in action logger for undo/redo mechanism to work
bool PerformanceView::anyChangesToLog() {
	if (performanceLayoutBackedUp) {
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			if (backupFXPress[xDisplay].previousKnobPosition != fxPress[xDisplay].previousKnobPosition) {
				return true;
			}
			else if (backupFXPress[xDisplay].currentKnobPosition != fxPress[xDisplay].currentKnobPosition) {
				return true;
			}
			else if (backupFXPress[xDisplay].yDisplay != fxPress[xDisplay].yDisplay) {
				return true;
			}
			else if (backupFXPress[xDisplay].timeLastPadPress != fxPress[xDisplay].timeLastPadPress) {
				return true;
			}
			else if (backupFXPress[xDisplay].padPressHeld != fxPress[xDisplay].padPressHeld) {
				return true;
			}
		}
	}
	return false;
}

/// called when you press <> + back
/// in param editor, it will clear existing param mappings
/// in regular performance view or value editor, it will clear held pads and reset param values to pre-held state
void PerformanceView::resetPerformanceView(ModelStackWithThreeMainThings* modelStack) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		// this could get called if you're loading a new song
		// don't need to reset performance view if you're loading a new song
		if (editingParam) {
			initLayout(layoutForPerformance[xDisplay]);
		}
		else if (fxPress[xDisplay].padPressHeld) {
			if (morphMode) {
				// if we're morphing using the morph encoder, don't reset held pads
				// because that will cause a momentary, audible, reset of the parameter value
				// let the morphing code take care of value changes for held pads
				// resetting held pads is retained for removing currently held pads that are not part
				// of the current morph layout
				if ((currentSong->performanceLayoutVariant == currentSong->performanceMorphLayoutAVariant)
				    && morphAFXPress[xDisplay].padPressHeld && !backupMorphALayout) {
					continue;
				}
				else if ((currentSong->performanceLayoutVariant == currentSong->performanceMorphLayoutBVariant)
				         && morphBFXPress[xDisplay].padPressHeld && !backupMorphBLayout) {
					continue;
				}
			}
			// obtain params::Kind and ParamID corresponding to the column in focus (xDisplay)
			params::Kind lastSelectedParamKind = layoutForPerformance[xDisplay].paramKind; // kind;
			int32_t lastSelectedParamID = layoutForPerformance[xDisplay].paramID;

			if (lastSelectedParamID != kNoSelection) {
				padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, false);
			}
		}
	}
	updateLayoutChangeStatus();
	if (morphMode) {
		renderMorphDisplay();
	}
	else {
		renderViewDisplay();
	}
	uiNeedsRendering(this);
}

/// resets a single FX column to remove held status
/// and reset the param value assigned to that FX column to pre-held state
void PerformanceView::resetFXColumn(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay) {
	if (fxPress[xDisplay].padPressHeld) {
		// obtain Kind and ParamID corresponding to the column in focus (xDisplay)
		params::Kind lastSelectedParamKind = layoutForPerformance[xDisplay].paramKind; // kind;
		int32_t lastSelectedParamID = layoutForPerformance[xDisplay].paramID;

		if (lastSelectedParamID != kNoSelection) {
			padReleaseAction(modelStack, lastSelectedParamKind, lastSelectedParamID, xDisplay, false);
		}
	}
	updateLayoutChangeStatus();
}

/// check if stutter is active and release it if it is
void PerformanceView::releaseStutter(ModelStackWithThreeMainThings* modelStack) {
	if (isUIModeActive(UI_MODE_STUTTERING)) {
		padReleaseAction(modelStack, params::Kind::UNPATCHED_GLOBAL, deluge::modulation::params::UNPATCHED_STUTTER_RATE,
		                 lastPadPress.xDisplay, false);
	}
}

/// this will set a new value for a parameter
/// if we're dealing with stutter, it will check if stutter is active and end the stutter first
/// if we're dealing with stutter, it will change the stutter rate value and then begin stutter
/// if you're in the value editor, pressing a column and changing the value will also open the sound editor
/// menu for the parameter to show you the current value in the menu
/// in regular performance view, this function will also update the parameter value shown on the display
bool PerformanceView::setParameterValue(ModelStackWithThreeMainThings* modelStack, params::Kind paramKind,
                                        int32_t paramID, int32_t xDisplay, int32_t knobPos, bool renderDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = currentSong->getModelStackWithParam(modelStack, paramID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		// if switching to a new pad in the stutter column and stuttering is already active
		// e.g. it means a pad was held before, end previous stutter before starting stutter again
		if (params::isParamStutter(paramKind, paramID) && (isUIModeActive(UI_MODE_STUTTERING))) {
			((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
			    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
		}

		if (fxPress[xDisplay].previousKnobPosition == kNoSelection) {
			int32_t oldParameterValue =
			    modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
			fxPress[xDisplay].previousKnobPosition =
			    modelStackWithParam->paramCollection->paramValueToKnobPos(oldParameterValue, modelStackWithParam);
		}

		int32_t newParameterValue =
		    modelStackWithParam->paramCollection->knobPosToParamValue(knobPos, modelStackWithParam);

		modelStackWithParam->autoParam->setValuePossiblyForRegion(newParameterValue, modelStackWithParam, view.modPos,
		                                                          view.modLength);

		if (!defaultEditingMode && params::isParamStutter(paramKind, paramID)
		    && (fxPress[xDisplay].previousKnobPosition != knobPos)) {
			((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
			    ->beginStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
		}

		if (renderDisplay) {
			if (params::isParamQuantizedStutter(paramKind, paramID)) {
				renderFXDisplay(paramKind, paramID, knobPos);
			}
			else {
				int32_t valueForDisplay = view.calculateKnobPosForDisplay(paramKind, paramID, knobPos + kKnobPosOffset);
				renderFXDisplay(paramKind, paramID, valueForDisplay);
			}
		}

		// this code could be called now if you midi learn the morph fader
		// so only send midi follow feedback if we're not in a clip context
		if (!getSelectedClip()) {
			// midi follow and midi feedback enabled
			// re-send midi cc because learned parameter value has changed
			view.sendMidiFollowFeedback(modelStackWithParam, knobPos);
		}

		return true;
	}

	return false;
}

/// get the current value for a parameter and update display if value is different than currently shown
/// update current value stored
void PerformanceView::getParameterValue(ModelStackWithThreeMainThings* modelStack, params::Kind paramKind,
                                        int32_t paramID, int32_t xDisplay, bool renderDisplay) {
	ModelStackWithAutoParam* modelStackWithParam = currentSong->getModelStackWithParam(modelStack, paramID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);

		int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);

		if (renderDisplay && (fxPress[xDisplay].currentKnobPosition != knobPos)) {
			if (params::isParamQuantizedStutter(paramKind, paramID)) {
				renderFXDisplay(paramKind, paramID, knobPos);
			}
			else {
				int32_t valueForDisplay = view.calculateKnobPosForDisplay(paramKind, paramID, knobPos + kKnobPosOffset);
				renderFXDisplay(paramKind, paramID, valueForDisplay);
			}
		}

		if (fxPress[xDisplay].currentKnobPosition != knobPos) {
			fxPress[xDisplay].currentKnobPosition = knobPos;
		}
	}
}

/// converts grid pad press yDisplay into a knobPosition value default
/// this will likely need to be customized based on the parameter to create some more param appropriate ranges
int32_t PerformanceView::calculateKnobPosForSinglePadPress(int32_t xDisplay, int32_t yDisplay) {
	int32_t newKnobPos = 0;

	params::Kind paramKind = defaultLayoutForPerformance[xDisplay].paramKind;
	int32_t paramID = defaultLayoutForPerformance[xDisplay].paramID;

	bool isDelayAmount = ((paramKind == params::Kind::UNPATCHED_GLOBAL) && (paramID == UNPATCHED_DELAY_AMOUNT));

	// if you press bottom pad, value is 0, for all other pads except for the top pad, value = row Y * 18
	// exception: delay amount increment is set to 9 by default

	if (yDisplay < 7) {
		newKnobPos =
		    yDisplay
		    * (isDelayAmount ? kParamValueIncrementForDelayAmount : kParamValueIncrementForAutomationSinglePadPress);
	}
	// if you are pressing the top pad, set the value to max (128)
	// exception: delay amount max value is set to 63 by default
	else {
		newKnobPos = isDelayAmount ? kMaxKnobPosForDelayAmount : kMaxKnobPos;
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

/// Used to edit a pad's value in editing mode
/// Also used to select morph layouts in morph mode
/// and used to edit sidebar actions such as loops remaining / repeats, etc.
void PerformanceView::selectEncoderAction(int8_t offset) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	if (morphMode && Buttons::isButtonPressed(deluge::hid::button::SYNTH)) {
		selectLayoutVariant(offset, currentSong->performanceMorphLayoutAVariant);
		backupMorphALayout = true;
		loadSelectedLayoutVariantFromFile(currentSong->performanceMorphLayoutAVariant, modelStack);
		backupMorphALayout = false;
		morphPosition = 0;
		setMorphLEDStates();
		renderMorphDisplay();
		return;
	}
	else if (morphMode && Buttons::isButtonPressed(deluge::hid::button::CV)) {
		selectLayoutVariant(offset, currentSong->performanceMorphLayoutBVariant);
		backupMorphBLayout = true;
		loadSelectedLayoutVariantFromFile(currentSong->performanceMorphLayoutBVariant, modelStack);
		backupMorphBLayout = false;
		morphPosition = kMaxKnobPos;
		setMorphLEDStates();
		renderMorphDisplay();
		return;
	}
	else if (lastPadPress.isActive && defaultEditingMode && !editingParam && (getCurrentUI() == &soundEditor)) {
		int32_t lastSelectedParamShortcutX = layoutForPerformance[lastPadPress.xDisplay].xDisplay;
		int32_t lastSelectedParamShortcutY = layoutForPerformance[lastPadPress.xDisplay].yDisplay;

		if (soundEditor.getCurrentMenuItem()
		    == paramShortcutsForSongView[lastSelectedParamShortcutX][lastSelectedParamShortcutY]) {
			getParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay, false);

			defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay] =
			    calculateKnobPosForSelectEncoderTurn(fxPress[lastPadPress.xDisplay].currentKnobPosition, offset);

			if (setParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay,
			                      defaultFXValues[lastPadPress.xDisplay][lastPadPress.yDisplay], false)) {
				updateLayoutChangeStatus();
			}
			return;
		}
	}
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->selectEncoderAction(offset);
	}
	else {
		if (currentSong->lastClipInstanceEnteredStartPos == -1) {
			sessionView.selectEncoderAction(offset);
		}
		else {
			arrangerView.selectEncoderAction(offset);
		}
	}
exit:
	return;
}

/// used to calculate new knobPos when you turn the select encoder
int32_t PerformanceView::calculateKnobPosForSelectEncoderTurn(int32_t knobPos, int32_t offset) {

	// adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		newKnobPos = knobPos;
	}
	else if ((knobPos + offset) <= kMaxKnobPos) {
		newKnobPos = knobPos + offset;
	}
	else if ((knobPos + offset) > kMaxKnobPos) {
		newKnobPos = kMaxKnobPos;
	}
	else {
		newKnobPos = knobPos;
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

int32_t PerformanceView::adjustKnobPosForQuantizedStutter(int32_t yDisplay) {
	int32_t knobPos = -kMinKnobPosForQuantizedStutter + (yDisplay * kParamValueIncrementForQuantizedStutter);
	return knobPos;
}

ActionResult PerformanceView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult PerformanceView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (currentSong->lastClipInstanceEnteredStartPos == -1) {
		sessionView.verticalEncoderAction(offset, inCardRoutine);
	}
	else {
		arrangerView.verticalEncoderAction(offset, inCardRoutine);
	}
	return ActionResult::DEALT_WITH;
}

/// why do I need this? (code won't compile without it)
uint32_t PerformanceView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

/// why do I need this? (code won't compile without it)
uint32_t PerformanceView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

/// updates the display if the mod encoder has just updated the same parameter currently being held / last held
/// if no param is currently being held, it will reset the display to just show "Performance View"
void PerformanceView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (morphMode && !defaultEditingMode) {
		morph(offset);
		renderMorphDisplay();
	}
	else {
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);

		if (!defaultEditingMode) {
			if (lastPadPress.isActive) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack =
				    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

				getParameterValue(modelStack, lastPadPress.paramKind, lastPadPress.paramID, lastPadPress.xDisplay);
			}
			else if (onFXDisplay) {
				renderViewDisplay();
			}
		}
	}
}

/// used to reset stutter if it's already active
void PerformanceView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	if (morphMode) {
		return;
	}
	// release stutter if it's already active before beginning stutter again
	if (on) {
		int32_t modKnobMode = -1;
		if (view.activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;

				// Stutter section
				if ((modKnobMode == 6) && (whichModEncoder == 1)) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithThreeMainThings* modelStack =
					    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

					releaseStutter(modelStack);

					uiNeedsRendering(this);

					if (onFXDisplay) {
						renderViewDisplay();
					}
				}
			}
		}
	}
	if (isUIModeActive(UI_MODE_STUTTERING) && lastPadPress.isActive
	    && params::isParamStutter(lastPadPress.paramKind, lastPadPress.paramID)) {
		return;
	}
	else {
		UI::modEncoderButtonAction(whichModEncoder, on);
	}
}

void PerformanceView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}

/// this compares the last loaded XML file defaults to the current layout in performance view
/// to determine if there are any unsaved changes
void PerformanceView::updateLayoutChangeStatus() {
	anyChangesToSave = false;

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		if (backupXMLDefaultLayoutForPerformance[xDisplay].paramKind != layoutForPerformance[xDisplay].paramKind) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultLayoutForPerformance[xDisplay].paramID != layoutForPerformance[xDisplay].paramID) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultFXPress[xDisplay].padPressHeld != fxPress[xDisplay].padPressHeld) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultFXPress[xDisplay].yDisplay != fxPress[xDisplay].yDisplay) {
			anyChangesToSave = true;
			break;
		}
		else if (backupXMLDefaultFXPress[xDisplay].previousKnobPosition != fxPress[xDisplay].previousKnobPosition) {
			anyChangesToSave = true;
			break;
		}
		else {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				if (backupXMLDefaultFXValues[xDisplay][yDisplay] != defaultFXValues[xDisplay][yDisplay]) {
					anyChangesToSave = true;
					break;
				}
			}
		}
	}

	// this could get called by the morph midi command, so only refresh if we're in performance view
	if (getCurrentUI() == this) {
		if (defaultEditingMode) {
			if (anyChangesToSave) {
				indicator_leds::blinkLed(IndicatorLED::SAVE);
			}
			else {
				indicator_leds::setLedState(IndicatorLED::SAVE, false);
			}
		}
		else {
			indicator_leds::setLedState(IndicatorLED::SAVE, false);
		}
	}

	return;
}

/// create folder /PERFORMANCE_VIEW/
/// determine the layout file name
/// append layout file name to folder path
/// append .XML to end of file name
void PerformanceView::setLayoutFilePath() {
	tempFilePath.set(PERFORM_DEFAULTS_FOLDER);
	tempFilePath.concatenate("/");
	if (currentSong->performanceLayoutVariant == 0) {
		tempFilePath.concatenate(PERFORM_DEFAULTS_XML);
	}
	else {
		char fileName[15];
		intToString(currentSong->performanceLayoutVariant, fileName);
		tempFilePath.concatenate(fileName);
		tempFilePath.concatenate(".XML");
	}
}

/// update saved performance view layout and update saved changes status
void PerformanceView::savePerformanceViewLayout() {
	setLayoutFilePath();
	writeDefaultsToFile();
	updateLayoutChangeStatus();
}

/// create default XML file and write defaults
void PerformanceView::writeDefaultsToFile() {
	// Default.xml / 1.xml, 2.xml ... 8.xml
	// if the file already exists, this will overwrite it.
	int32_t error = storageManager.createXMLFile(tempFilePath.get(), true);
	if (error) {
		return;
	}

	//<defaults>
	storageManager.writeOpeningTagBeginning(PERFORM_DEFAULTS_TAG);
	storageManager.writeOpeningTagEnd();

	//<defaultFXValues>
	storageManager.writeOpeningTagBeginning(PERFORM_DEFAULTS_FXVALUES_TAG);
	storageManager.writeOpeningTagEnd();

	writeDefaultFXValuesToFile();

	storageManager.writeClosingTag(PERFORM_DEFAULTS_FXVALUES_TAG);

	storageManager.writeClosingTag(PERFORM_DEFAULTS_TAG);

	storageManager.closeFileAfterWriting();

	anyChangesToSave = false;
}

/// creates "FX1 - FX16 tags"
/// limiting # of FX to the # of columns on the grid (16 = kDisplayWidth)
/// could expand # of FX in the future if we allow user to selected from a larger bank of FX / build their own
/// FX
void PerformanceView::writeDefaultFXValuesToFile() {
	char tagName[10];
	tagName[0] = 'F';
	tagName[1] = 'X';
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		intToString(xDisplay + 1, &tagName[2]);
		storageManager.writeOpeningTagBeginning(tagName);
		storageManager.writeOpeningTagEnd();
		writeDefaultFXParamToFile(xDisplay);
		writeDefaultFXRowValuesToFile(xDisplay);
		writeDefaultFXHoldStatusToFile(xDisplay);
		storageManager.writeClosingTag(tagName);
	}
}

/// convert paramID to a paramName to write to XML
void PerformanceView::writeDefaultFXParamToFile(int32_t xDisplay) {
	char const* paramName;

	auto kind = layoutForPerformance[xDisplay].paramKind;
	if (kind == params::Kind::UNPATCHED_GLOBAL) {
		paramName = params::paramNameForFile(kind, params::UNPATCHED_START + layoutForPerformance[xDisplay].paramID);
	}
	else {
		paramName = PERFORM_DEFAULTS_NO_PARAM;
	}
	//<param>
	storageManager.writeTag(PERFORM_DEFAULTS_PARAM_TAG, paramName);

	memcpy(&backupXMLDefaultLayoutForPerformance[xDisplay], &layoutForPerformance[xDisplay],
	       sizeof(ParamsForPerformance));
}

/// creates "8 - 1 row # tags within a "row" tag"
/// limiting # of rows to the # of rows on the grid (8 = kDisplayHeight)
void PerformanceView::writeDefaultFXRowValuesToFile(int32_t xDisplay) {
	//<row>
	storageManager.writeOpeningTagBeginning(PERFORM_DEFAULTS_ROW_TAG);
	storageManager.writeOpeningTagEnd();
	char rowNumber[5];
	// creates tags from row 8 down to row 1
	for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
		intToString(yDisplay + 1, rowNumber);
		storageManager.writeTag(rowNumber, defaultFXValues[xDisplay][yDisplay] + kKnobPosOffset);

		backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];
	}
	storageManager.writeClosingTag(PERFORM_DEFAULTS_ROW_TAG);
}

/// for each FX column, write the held status, what row is being held, and what previous value was
/// (previous value is used to reset param after you remove the held status)
void PerformanceView::writeDefaultFXHoldStatusToFile(int32_t xDisplay) {
	//<hold>
	storageManager.writeOpeningTagBeginning(PERFORM_DEFAULTS_HOLD_TAG);
	storageManager.writeOpeningTagEnd();

	if (fxPress[xDisplay].padPressHeld) {
		//<status>
		storageManager.writeTag(PERFORM_DEFAULTS_HOLD_STATUS_TAG, PERFORM_DEFAULTS_ON);
		//<row>
		storageManager.writeTag(PERFORM_DEFAULTS_ROW_TAG, fxPress[xDisplay].yDisplay + 1);
		//<resetValue>
		storageManager.writeTag(PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG,
		                        fxPress[xDisplay].previousKnobPosition + kKnobPosOffset);

		memcpy(&backupXMLDefaultFXPress[xDisplay], &fxPress[xDisplay], sizeof(FXColumnPress));
	}
	else {
		//<status>
		storageManager.writeTag(PERFORM_DEFAULTS_HOLD_STATUS_TAG, PERFORM_DEFAULTS_OFF);
		//<row>
		storageManager.writeTag(PERFORM_DEFAULTS_ROW_TAG, kNoSelection);
		//<resetValue>
		storageManager.writeTag(PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG, kNoSelection);

		initFXPress(backupXMLDefaultFXPress[xDisplay]);
	}

	storageManager.writeClosingTag(PERFORM_DEFAULTS_HOLD_TAG);
}

/// backup current layout, load saved layout, log layout change, update change status
void PerformanceView::loadPerformanceViewLayout(ModelStackWithThreeMainThings* modelStack) {
	resetPerformanceView(modelStack);
	if (successfullyReadDefaultsFromFile) {
		readDefaultsFromBackedUpFile(modelStack);
	}
	else {
		readDefaultsFromFile(modelStack);
	}
	layoutUpdated();
}

/// after a layout has been loaded, we want to back it up so we can re-load it more quickly next time
/// and also be able to do comparisons of changes to the backed up layout
/// we also delete all action logs so that you can't undo after loading a layout
void PerformanceView::layoutUpdated() {
	actionLogger.deleteAllLogs();
	backupPerformanceLayout();
	updateLayoutChangeStatus();
	// this could get called by the morph midi command, so only refresh if we're in performance view
	if (getCurrentUI() == this) {
		uiNeedsRendering(this);
	}
}

/// re-read defaults from backed up XML in memory in order to reduce SD Card IO
void PerformanceView::readDefaultsFromBackedUpFile(ModelStackWithThreeMainThings* modelStack) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		memcpy(&layoutForPerformance[xDisplay], &backupXMLDefaultLayoutForPerformance[xDisplay],
		       sizeof(ParamsForPerformance));

		memcpy(&fxPress[xDisplay], &backupXMLDefaultFXPress[xDisplay], sizeof(FXColumnPress));

		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			defaultFXValues[xDisplay][yDisplay] = backupXMLDefaultFXValues[xDisplay][yDisplay];
		}

		initializeHeldFX(xDisplay, modelStack);
	}
}

/// read defaults from XML
void PerformanceView::readDefaultsFromFile(ModelStackWithThreeMainThings* modelStack) {
	// no need to keep reading from SD card after first load
	if (successfullyReadDefaultsFromFile) {
		return;
	}

	setLayoutFilePath();

	FilePointer fp;
	// performanceView.XML
	bool success = storageManager.fileExists(tempFilePath.get(), &fp);
	if (!success) {
		loadDefaultLayout();
		return;
	}

	//<defaults>
	int32_t error = storageManager.openXMLFile(&fp, PERFORM_DEFAULTS_TAG);
	if (error) {
		loadDefaultLayout();
		return;
	}

	char const* tagName;
	// step into the <defaultFXValues> tag
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, PERFORM_DEFAULTS_FXVALUES_TAG)) {
			readDefaultFXValuesFromFile(modelStack);
		}
		storageManager.exitTag();
	}

	storageManager.closeFile();

	successfullyReadDefaultsFromFile = true;
}

/// if no XML file exists, load default layout (paramKind, paramID, xDisplay, yDisplay, rowColour,
/// rowTailColour)
void PerformanceView::loadDefaultLayout() {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		memcpy(&layoutForPerformance[xDisplay], &defaultLayoutForPerformance[xDisplay], sizeof(ParamsForPerformance));
		memcpy(&backupXMLDefaultLayoutForPerformance[xDisplay], &defaultLayoutForPerformance[xDisplay],
		       sizeof(ParamsForPerformance));
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (params::isParamQuantizedStutter(layoutForPerformance[xDisplay].paramKind,
			                                    layoutForPerformance[xDisplay].paramID)) {
				defaultFXValues[xDisplay][yDisplay] = adjustKnobPosForQuantizedStutter(yDisplay);
				backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];
			}
		}
		initFXPress(fxPress[xDisplay]);
		initFXPress(backupXMLDefaultFXPress[xDisplay]);
	}
	currentSong->performanceLayoutVariant = 0;
	successfullyReadDefaultsFromFile = true;
}

void PerformanceView::readDefaultFXValuesFromFile(ModelStackWithThreeMainThings* modelStack) {
	char const* tagName;
	char tagNameFX[5];
	tagNameFX[0] = 'F';
	tagNameFX[1] = 'X';

	// loop through all FX number tags
	//<FX#>
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		// find the FX number that the tag corresponds to
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			intToString(xDisplay + 1, &tagNameFX[2]);

			if (!strcmp(tagName, tagNameFX)) {
				readDefaultFXParamAndRowValuesFromFile(xDisplay, modelStack);
				break;
			}
		}
		storageManager.exitTag();
	}
}

void PerformanceView::readDefaultFXParamAndRowValuesFromFile(int32_t xDisplay,
                                                             ModelStackWithThreeMainThings* modelStack) {
	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//<param>
		if (!strcmp(tagName, PERFORM_DEFAULTS_PARAM_TAG)) {
			readDefaultFXParamFromFile(xDisplay);
		}
		//<row>
		else if (!strcmp(tagName, PERFORM_DEFAULTS_ROW_TAG)) {
			readDefaultFXRowNumberValuesFromFile(xDisplay);
		}
		//<hold>
		else if (!strcmp(tagName, PERFORM_DEFAULTS_HOLD_TAG)) {
			readDefaultFXHoldStatusFromFile(xDisplay, modelStack);
		}
		storageManager.exitTag();
	}
}

/// compares param name from <param> tag to the list of params available for use in performance view
/// if param is found, it loads the layout info for that param into the view (paramKind, paramID, xDisplay,
/// yDisplay, rowColour, rowTailColour)
void PerformanceView::readDefaultFXParamFromFile(int32_t xDisplay) {
	char const* paramName;
	char const* tagName = storageManager.readTagOrAttributeValue();

	for (int32_t i = 0; i < kNumParamsForPerformance; i++) {
		paramName = params::paramNameForFile(songParamsForPerformance[i].paramKind,
		                                     params::UNPATCHED_START + songParamsForPerformance[i].paramID);
		if (!strcmp(tagName, paramName)) {
			memcpy(&layoutForPerformance[xDisplay], &songParamsForPerformance[i], sizeof(ParamsForPerformance));

			memcpy(&backupXMLDefaultLayoutForPerformance[xDisplay], &layoutForPerformance[xDisplay],
			       sizeof(ParamsForPerformance));
			if (backupMorphALayout) {
				memcpy(&morphALayoutForPerformance[xDisplay], &layoutForPerformance[xDisplay],
				       sizeof(ParamsForPerformance));
			}
			if (backupMorphBLayout) {
				memcpy(&morphBLayoutForPerformance[xDisplay], &layoutForPerformance[xDisplay],
				       sizeof(ParamsForPerformance));
			}
			break;
		}
	}
}

/// this will load the values corresponding to each pad in each column in performance view
void PerformanceView::readDefaultFXRowNumberValuesFromFile(int32_t xDisplay) {
	char const* tagName;
	char rowNumber[5];
	// loop through all row <#> number tags
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		// find the row number that the tag corresponds to
		// reads from row 8 down to row 1
		for (int32_t yDisplay = kDisplayHeight - 1; yDisplay >= 0; yDisplay--) {
			intToString(yDisplay + 1, rowNumber);
			if (!strcmp(tagName, rowNumber)) {
				defaultFXValues[xDisplay][yDisplay] = storageManager.readTagOrAttributeValueInt() - kKnobPosOffset;

				// check if a value greater than 64 was entered as a default value in xml file
				if (defaultFXValues[xDisplay][yDisplay] > kKnobPosOffset) {
					defaultFXValues[xDisplay][yDisplay] = kKnobPosOffset;
				}

				if (params::isParamQuantizedStutter(layoutForPerformance[xDisplay].paramKind,
				                                    layoutForPerformance[xDisplay].paramID)) {
					defaultFXValues[xDisplay][yDisplay] = adjustKnobPosForQuantizedStutter(yDisplay);
				}

				backupXMLDefaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];

				if (backupMorphALayout) {
					morphAFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];
				}

				if (backupMorphBLayout) {
					morphBFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay];
				}

				break;
			}
		}
		storageManager.exitTag();
	}
}

/// this function reads layout data relating to held pads
/// this includes, held status, held value and previous value to reset back to if hold is removed
void PerformanceView::readDefaultFXHoldStatusFromFile(int32_t xDisplay, ModelStackWithThreeMainThings* modelStack) {
	char const* tagName;
	// loop through the hold tags
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//<status>
		if (!strcmp(tagName, PERFORM_DEFAULTS_HOLD_STATUS_TAG)) {
			char const* holdStatus = storageManager.readTagOrAttributeValue();
			if (!strcmp(holdStatus, PERFORM_DEFAULTS_ON)) {
				if (!params::isParamStutter(layoutForPerformance[xDisplay].paramKind,
				                            layoutForPerformance[xDisplay].paramID)) {
					fxPress[xDisplay].padPressHeld = true;
					fxPress[xDisplay].timeLastPadPress = AudioEngine::audioSampleTimer;

					backupXMLDefaultFXPress[xDisplay].padPressHeld = fxPress[xDisplay].padPressHeld;
					backupXMLDefaultFXPress[xDisplay].timeLastPadPress = fxPress[xDisplay].timeLastPadPress;

					if (backupMorphALayout) {
						morphAFXPress[xDisplay].padPressHeld = fxPress[xDisplay].padPressHeld;
						morphAFXPress[xDisplay].timeLastPadPress = fxPress[xDisplay].timeLastPadPress;
					}

					if (backupMorphBLayout) {
						morphBFXPress[xDisplay].padPressHeld = fxPress[xDisplay].padPressHeld;
						morphBFXPress[xDisplay].timeLastPadPress = fxPress[xDisplay].timeLastPadPress;
					}
				}
			}
		}
		//<row>
		else if (!strcmp(tagName, PERFORM_DEFAULTS_ROW_TAG)) {
			int32_t yDisplay = storageManager.readTagOrAttributeValueInt();
			if ((yDisplay >= 1) && (yDisplay <= 8)) {
				fxPress[xDisplay].yDisplay = yDisplay - 1;
				fxPress[xDisplay].currentKnobPosition = defaultFXValues[xDisplay][fxPress[xDisplay].yDisplay];

				backupXMLDefaultFXPress[xDisplay].yDisplay = fxPress[xDisplay].yDisplay;
				backupXMLDefaultFXPress[xDisplay].currentKnobPosition = fxPress[xDisplay].currentKnobPosition;

				if (backupMorphALayout) {
					morphAFXPress[xDisplay].yDisplay = fxPress[xDisplay].yDisplay;
					morphAFXPress[xDisplay].currentKnobPosition = fxPress[xDisplay].currentKnobPosition;
				}

				if (backupMorphBLayout) {
					morphBFXPress[xDisplay].yDisplay = fxPress[xDisplay].yDisplay;
					morphBFXPress[xDisplay].currentKnobPosition = fxPress[xDisplay].currentKnobPosition;
				}
			}
		}
		//<resetValue>
		else if (!strcmp(tagName, PERFORM_DEFAULTS_HOLD_RESETVALUE_TAG)) {
			fxPress[xDisplay].previousKnobPosition = storageManager.readTagOrAttributeValueInt() - kKnobPosOffset;
			// check if a value greater than 64 was entered as a default value in xml file
			if (fxPress[xDisplay].previousKnobPosition > kKnobPosOffset) {
				fxPress[xDisplay].previousKnobPosition = kKnobPosOffset;
			}
			backupXMLDefaultFXPress[xDisplay].previousKnobPosition = fxPress[xDisplay].previousKnobPosition;

			if (backupMorphALayout) {
				morphAFXPress[xDisplay].previousKnobPosition = fxPress[xDisplay].previousKnobPosition;
			}

			if (backupMorphBLayout) {
				morphBFXPress[xDisplay].previousKnobPosition = fxPress[xDisplay].previousKnobPosition;
			}
		}
		storageManager.exitTag();
	}
	initializeHeldFX(xDisplay, modelStack);
}

/// if there are any held pads in a layout, this function will initialize them by
/// changing parameter values to the held value
void PerformanceView::initializeHeldFX(int32_t xDisplay, ModelStackWithThreeMainThings* modelStack) {
	if (fxPress[xDisplay].padPressHeld) {
		// set the value associated with the held pad
		if ((fxPress[xDisplay].currentKnobPosition != kNoSelection)
		    && (fxPress[xDisplay].previousKnobPosition != kNoSelection)) {
			if ((layoutForPerformance[xDisplay].paramKind != params::Kind::NONE)
			    && (layoutForPerformance[xDisplay].paramID != kNoSelection)) {
				setParameterValue(modelStack, layoutForPerformance[xDisplay].paramKind,
				                  layoutForPerformance[xDisplay].paramID, xDisplay,
				                  defaultFXValues[xDisplay][fxPress[xDisplay].yDisplay], false);
			}
		}
	}
	else {
		initFXPress(fxPress[xDisplay]);
		initFXPress(backupXMLDefaultFXPress[xDisplay]);
		if (backupMorphALayout) {
			initFXPress(morphAFXPress[xDisplay]);
		}
		if (backupMorphBLayout) {
			initFXPress(morphBFXPress[xDisplay]);
		}
	}
}

/// when a song is loaded, we want to load the layout settings that were saved with the song
void PerformanceView::initializeLayoutVariantsFromSong() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	// backup the current layout variant because it will be temporarily overriden
	// when the morph layouts are loaded from file
	// we then restore the current variant below when the current layout variant is loaded
	int32_t currentVariant = currentSong->performanceLayoutVariant;

	backupMorphALayout = true;
	loadSelectedLayoutVariantFromFile(currentSong->performanceMorphLayoutAVariant, modelStack);
	backupMorphALayout = false;

	backupMorphBLayout = true;
	loadSelectedLayoutVariantFromFile(currentSong->performanceMorphLayoutBVariant, modelStack);
	backupMorphBLayout = false;

	loadSelectedLayoutVariantFromFile(currentVariant, modelStack);
}

/// used in morph mode with the select encoder to select morph layout variant assigned to A and B
void PerformanceView::selectLayoutVariant(int32_t offset, int32_t& variant) {
	if (variant == kNoSelection) {
		if (offset > 0) {
			variant = -1;
		}
		else if (offset < 0) {
			variant = kMaxPerformanceLayoutVariants;
		}
	}
	variant += offset;
	if (variant < 0) {
		variant = kMaxPerformanceLayoutVariants - 1;
	}
	else if (variant > (kMaxPerformanceLayoutVariants - 1)) {
		variant = 0;
	}
	displayLayoutVariant(variant);
}

/// displays if no layout has been loaded, the default layout has been loaded or layouts 1 to 8
void PerformanceView::displayLayoutVariant(int32_t variant) {
	if (variant == kNoSelection) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NONE));
	}
	else if (variant == 0) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PERFORM_DEFAULT_LAYOUT));
	}
	else {
		char buffer[10];
		intToString(variant, buffer);
		display->displayPopup(buffer);
	}
}

/// here we load a layout variant from its XML file
void PerformanceView::loadSelectedLayoutVariantFromFile(int32_t variant, ModelStackWithThreeMainThings* modelStack) {
	if (variant != kNoSelection) {
		currentSong->performanceLayoutVariant = variant;
		successfullyReadDefaultsFromFile = false;
		loadPerformanceViewLayout(modelStack);
		if (morphMode) {
			renderMorphDisplay();
		}
		else {
			renderViewDisplay();
		}
	}
}

/// enter morph mode, set the led states and render display
void PerformanceView::enterMorphMode() {
	morphMode = true;
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, true);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, true);
	setMorphLEDStates();
	view.setModLedStates();
	renderMorphDisplay();
}

/// exit morph mode, set the led states and render display
void PerformanceView::exitMorphMode() {
	morphMode = false;
	setCentralLEDStates();
	view.setKnobIndicatorLevels();
	view.setModLedStates();
	renderViewDisplay();
}

/// received morph cc from global midi command MORPH
void PerformanceView::receivedMorphCC(int32_t value) {
	if (value == kMaxMIDIValue) {
		value = kMaxKnobPos;
	}
	int32_t offset = value - morphPosition;
	morph(offset, true);
}

/// this function determines if morphing is possible
/// if it is possible, it adjusts the morph position and obtains current morph values
/// and morphs towards the target morph layout (A or B) based on the direction (is offset pos. or neg.)
void PerformanceView::morph(int32_t offset, bool isMIDICommand) {
	if (offset != 0 && (isMIDICommand || isMorphingPossible())) {
		int32_t currentMorphPosition = morphPosition;
		adjustMorphPosition(offset);

		// loop through every performance column on the grid
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			params::Kind paramKind = layoutForPerformance[xDisplay].paramKind;
			int32_t paramID = layoutForPerformance[xDisplay].paramID;

			// no morphing stutter
			if (params::isParamStutter(paramKind, paramID)) {
				continue;
			}

			// if no parameter value is being held in either the Morph A or B layouts, then no morphing is possible
			if (!morphAFXPress[xDisplay].padPressHeld && !morphBFXPress[xDisplay].padPressHeld) {
				continue;
			}

			int32_t sourceKnobPosition;
			int32_t targetKnobPosition;
			// morph from A to B
			if (offset > 0) {
				// if there is a held pad in layout A, use the held value as starting morph position
				if (morphAFXPress[xDisplay].padPressHeld) {
					sourceKnobPosition = morphAFXPress[xDisplay].currentKnobPosition;
				}
				// if there is no held pad in layout A, there must be a held pad in layout B
				// so morph towards snapshotted value from layout B
				else {
					sourceKnobPosition = morphBFXPress[xDisplay].previousKnobPosition;
				}
				// if there is a held pad in layout B, use the held value as the ending morph position
				if (morphBFXPress[xDisplay].padPressHeld) {
					targetKnobPosition = morphBFXPress[xDisplay].currentKnobPosition;
				}
				// if there is no held pad in layout B, there must be a held pad in layout A
				// so morph towards snapshotted value from layout A
				else {
					targetKnobPosition = morphAFXPress[xDisplay].previousKnobPosition;
				}
			}
			// morph from B to A
			else if (offset < 0) {
				// if there is a held pad in layout B, use the held value as starting morph position
				if (morphBFXPress[xDisplay].padPressHeld) {
					sourceKnobPosition = morphBFXPress[xDisplay].currentKnobPosition;
				}
				// if there is no held pad in layout B, there must be a held pad in layout A
				// so morph towards snapshotted value from layout A
				else {
					sourceKnobPosition = morphAFXPress[xDisplay].previousKnobPosition;
				}
				// if there is a held pad in layout A, use the held value as the ending morph position
				if (morphAFXPress[xDisplay].padPressHeld) {
					targetKnobPosition = morphAFXPress[xDisplay].currentKnobPosition;
				}
				// if there is no held pad in layout A, there must be a held pad in layout B
				// so morph towards snapshotted value from layout B
				else {
					targetKnobPosition = morphBFXPress[xDisplay].previousKnobPosition;
				}
			}
			if (sourceKnobPosition == kNoSelection) {
				continue;
			}
			if (targetKnobPosition == kNoSelection) {
				continue;
			}
			if (sourceKnobPosition != targetKnobPosition) {
				morphTowardsTarget(paramKind, paramID, sourceKnobPosition + kKnobPosOffset,
				                   targetKnobPosition + kKnobPosOffset, offset);
			}
		}

		// check if morph position has changed
		if (currentMorphPosition != morphPosition) {
			if (morphPosition == 0 || morphPosition == kMaxKnobPos) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack =
				    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
				// have we landed on the final Morph A position?
				// if yes, fully load that backup as the current layout
				if (morphPosition == 0) {
					loadMorphALayout(modelStack);
				}
				// have we landed on the final Morph B position?
				// if yes, fully load that backup as the current layout
				else if (morphPosition == kMaxKnobPos) {
					loadMorphBLayout(modelStack);
				}
			}
			else {
				renderMorphDisplay();
			}
		}
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PERFORM_CANT_MORPH));
	}
}

/// determines if morphing is possible
/// morphing is possible if:
/// 1) a layout variant has been assigned to both morph positions A and B
/// 2) a parameter has been assigned to every column in both layouts
/// 3) the parameters assigned to each column in both layouts are the same
/// 4) you haven't assigned stutter to every column
bool PerformanceView::isMorphingPossible() {
	if ((currentSong->performanceMorphLayoutAVariant != kNoSelection)
	    && (currentSong->performanceMorphLayoutBVariant != kNoSelection)) {
		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			if ((morphALayoutForPerformance[xDisplay].paramKind == params::Kind::NONE)
			    || (morphALayoutForPerformance[xDisplay].paramID == kNoSelection)) {
				return false;
			}

			if ((morphBLayoutForPerformance[xDisplay].paramKind == params::Kind::NONE)
			    || (morphBLayoutForPerformance[xDisplay].paramID == kNoSelection)) {
				return false;
			}

			// no morphing stutter
			if (params::isParamStutter(layoutForPerformance[xDisplay].paramKind,
			                           layoutForPerformance[xDisplay].paramID)) {
				continue;
			}

			// let's make sure the layout's are compatible for morphing
			if ((morphALayoutForPerformance[xDisplay].paramKind == morphBLayoutForPerformance[xDisplay].paramKind)
			    && (morphALayoutForPerformance[xDisplay].paramID == morphBLayoutForPerformance[xDisplay].paramID)) {
				// if they're compatible, is there a held value in either layout?
				if (morphAFXPress[xDisplay].padPressHeld || morphBFXPress[xDisplay].padPressHeld) {
					return true;
				}
			}
		}
	}
	return false;
}

void PerformanceView::adjustMorphPosition(int32_t offset) {
	morphPosition += offset;
	if (morphPosition < 0) {
		morphPosition = 0;
	}
	else if (morphPosition > kMaxKnobPos) {
		morphPosition = kMaxKnobPos;
	}
	setMorphLEDStates();
}

/// linearly interpolates and sets the current value to the next value in the direction of the
/// target value in the layout variant we are morphing towards
void PerformanceView::morphTowardsTarget(params::Kind paramKind, int32_t paramID, int32_t sourceKnobPosition,
                                         int32_t targetKnobPosition, int32_t offset) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	ModelStackWithAutoParam* modelStackWithParam = currentSong->getModelStackWithParam(modelStack, paramID);

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		float floatMorphPosition = static_cast<float>(morphPosition);
		float floatKnobPositionDifference =
		    static_cast<float>(targetKnobPosition) - static_cast<float>(sourceKnobPosition);
		float floatMorphKnobPosition;

		// morphing towards B
		if (offset > 0) {
			floatMorphKnobPosition =
			    sourceKnobPosition + std::round(((floatMorphPosition / kMaxKnobPos) * floatKnobPositionDifference));
		}
		// morphing towards A
		else if (offset < 0) {
			floatMorphKnobPosition =
			    sourceKnobPosition
			    + std::round((((kMaxKnobPos - floatMorphPosition) / kMaxKnobPos) * floatKnobPositionDifference));
		}

		int32_t morphKnobPosition = static_cast<int32_t>(floatMorphKnobPosition);

		int32_t newParameterValue = modelStackWithParam->paramCollection->knobPosToParamValue(
		    morphKnobPosition - kKnobPosOffset, modelStackWithParam);

		modelStackWithParam->autoParam->setValuePossiblyForRegion(newParameterValue, modelStackWithParam, view.modPos,
		                                                          view.modLength);
	}
}

/// when you reach the morph position corresponding to layout A
/// re-load the A morph layout so that held pads are set and current layout is updated
/// note to self: I'm not sure this is entirely necessary, could probably just copy the fxPress info
/// and the default values info over from the morph layout to the current layout
/// e.g. no need to reset the view, initialize held pads or update the layout
void PerformanceView::loadMorphALayout(ModelStackWithThreeMainThings* modelStack) {
	if (currentSong->performanceMorphLayoutAVariant != kNoSelection) {
		resetPerformanceView(modelStack);

		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				defaultFXValues[xDisplay][yDisplay] = morphAFXValues[xDisplay][yDisplay];
			}

			if (fxPress[xDisplay].currentKnobPosition == kNoSelection) {
				memcpy(&fxPress[xDisplay], &morphAFXPress[xDisplay], sizeof(FXColumnPress));

				if (morphPosition != 0) {
					initializeHeldFX(xDisplay, modelStack);
				}
			}
		}
		currentSong->performanceLayoutVariant = currentSong->performanceMorphLayoutAVariant;
		morphPosition = 0;
		layoutUpdated();
		setMorphLEDStates();
		renderMorphDisplay();
	}
}

/// when you reach the morph position corresponding to layout B
/// re-load the B morph layout so that held pads are set and current layout is updated
/// note to self: I'm not sure this is entirely necessary, could probably just copy the fxPress info
/// and the default values info over from the morph layout to the current layout
/// e.g. no need to reset the view, initialize held pads or update the layout
void PerformanceView::loadMorphBLayout(ModelStackWithThreeMainThings* modelStack) {
	if (currentSong->performanceMorphLayoutBVariant != kNoSelection) {
		resetPerformanceView(modelStack);

		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				defaultFXValues[xDisplay][yDisplay] = morphBFXValues[xDisplay][yDisplay];
			}

			if (fxPress[xDisplay].currentKnobPosition == kNoSelection) {
				memcpy(&fxPress[xDisplay], &morphBFXPress[xDisplay], sizeof(FXColumnPress));

				if (morphPosition != kMaxKnobPos) {
					initializeHeldFX(xDisplay, modelStack);
				}
			}
		}
		currentSong->performanceLayoutVariant = currentSong->performanceMorphLayoutBVariant;
		morphPosition = kMaxKnobPos;
		layoutUpdated();
		setMorphLEDStates();
		renderMorphDisplay();
	}
}

/// set led states for morph mode and for exiting morph mode
void PerformanceView::setMorphLEDStates() {
	if (getCurrentUI() == this) {
		// this could get called by the morph midi command, so only refresh if we're in performance view
		if (morphMode && isMorphingPossible()) {
			if (morphPosition == 0) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, true);
				indicator_leds::setLedState(IndicatorLED::KIT, false);
				indicator_leds::setLedState(IndicatorLED::MIDI, false);
				indicator_leds::setLedState(IndicatorLED::CV, false);
			}
			else if ((morphPosition > 0) && (morphPosition < 32)) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, true);
				indicator_leds::setLedState(IndicatorLED::KIT, true);
				indicator_leds::setLedState(IndicatorLED::MIDI, false);
				indicator_leds::setLedState(IndicatorLED::CV, false);
			}
			else if ((morphPosition >= 32) && (morphPosition < 64)) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, false);
				indicator_leds::setLedState(IndicatorLED::KIT, true);
				indicator_leds::setLedState(IndicatorLED::MIDI, false);
				indicator_leds::setLedState(IndicatorLED::CV, false);
			}
			else if (morphPosition == 64) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, false);
				indicator_leds::setLedState(IndicatorLED::KIT, true);
				indicator_leds::setLedState(IndicatorLED::MIDI, true);
				indicator_leds::setLedState(IndicatorLED::CV, false);
			}
			else if ((morphPosition > 64) && (morphPosition < 96)) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, false);
				indicator_leds::setLedState(IndicatorLED::KIT, false);
				indicator_leds::setLedState(IndicatorLED::MIDI, true);
				indicator_leds::setLedState(IndicatorLED::CV, false);
			}
			else if ((morphPosition >= 96) && (morphPosition < kMaxKnobPos)) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, false);
				indicator_leds::setLedState(IndicatorLED::KIT, false);
				indicator_leds::setLedState(IndicatorLED::MIDI, true);
				indicator_leds::setLedState(IndicatorLED::CV, true);
			}
			else if (morphPosition == kMaxKnobPos) {
				indicator_leds::setLedState(IndicatorLED::SYNTH, false);
				indicator_leds::setLedState(IndicatorLED::KIT, false);
				indicator_leds::setLedState(IndicatorLED::MIDI, false);
				indicator_leds::setLedState(IndicatorLED::CV, true);
			}
		}
		else {
			indicator_leds::setLedState(IndicatorLED::SYNTH, false);
			indicator_leds::setLedState(IndicatorLED::KIT, false);
			indicator_leds::setLedState(IndicatorLED::MIDI, false);
			indicator_leds::setLedState(IndicatorLED::CV, false);
		}
		setKnobIndicatorLevels();
	}
}

/// set knob indicator levels for morph mode and for exiting morph mode
void PerformanceView::setKnobIndicatorLevels() {
	if (morphMode) {
		if (isMorphingPossible()) {
			indicator_leds::setKnobIndicatorLevel(0, morphPosition);
			indicator_leds::setKnobIndicatorLevel(1, morphPosition);
			if (morphPosition == 64) {
				indicator_leds::blinkKnobIndicator(0);
				indicator_leds::blinkKnobIndicator(1);

				// Make it harder to turn that knob away from its centred position
				view.pretendModKnobsUntouchedForAWhile();
			}
			else {
				indicator_leds::stopBlinkingKnobIndicator(0);
				indicator_leds::stopBlinkingKnobIndicator(1);
			}
		}
		else {
			indicator_leds::clearKnobIndicatorLevels();
		}
	}
	else {
		view.setKnobIndicatorLevels();
	}
}
