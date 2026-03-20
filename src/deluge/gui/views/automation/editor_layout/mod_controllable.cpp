/*
 * Copyright (c) 2023 Sean Ditny
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

#include "gui/views/automation/editor_layout/mod_controllable.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"

// namespace deluge::gui::views::automation::editor_layout {

namespace params = deluge::modulation::params;

using namespace deluge::gui;

constexpr int32_t kParamNodeWidth = 3;

// VU meter style colours for the automation editor

const RGB rowColour[kDisplayHeight] = {{0, 255, 0},   {36, 219, 0}, {73, 182, 0}, {109, 146, 0},
                                       {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const RGB rowTailColour[kDisplayHeight] = {{2, 53, 2},  {9, 46, 2},  {17, 38, 2}, {24, 31, 2},
                                           {31, 24, 2}, {38, 17, 2}, {46, 9, 2},  {53, 2, 2}};

const RGB rowBlurColour[kDisplayHeight] = {{71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57},
                                           {76, 70, 53},  {77, 60, 48},  {78, 49, 44}, {79, 39, 39}};

const RGB rowBipolarDownColour[kDisplayHeight / 2] = {{255, 0, 0}, {182, 73, 0}, {73, 182, 0}, {0, 255, 0}};

const RGB rowBipolarDownTailColour[kDisplayHeight / 2] = {{53, 2, 2}, {38, 17, 2}, {17, 38, 2}, {2, 53, 2}};

const RGB rowBipolarDownBlurColour[kDisplayHeight / 2] = {{79, 39, 39}, {77, 60, 48}, {73, 90, 62}, {71, 111, 71}};

// lookup tables for the values that are set when you press the pads in each row of the grid
const int32_t nonPatchCablePadPressValues[kDisplayHeight] = {0, 18, 37, 55, 73, 91, 110, 128};
const int32_t patchCablePadPressValues[kDisplayHeight] = {-128, -90, -60, -30, 30, 60, 90, 128};

// lookup tables for the min value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMinPadDisplayValues[kDisplayHeight] = {0, 17, 33, 49, 65, 81, 97, 113};
const int32_t patchCableMinPadDisplayValues[kDisplayHeight] = {-128, -96, -64, -32, 1, 33, 65, 97};

// lookup tables for the max value of each pad's value range used to display automation on each row of the grid
const int32_t nonPatchCableMaxPadDisplayValues[kDisplayHeight] = {16, 32, 48, 64, 80, 96, 112, 128};
const int32_t patchCableMaxPadDisplayValues[kDisplayHeight] = {-97, -65, -33, -1, 32, 64, 96, 128};

// summary of pad ranges and press values (format: MIN < PRESS < MAX)
// patch cable:
// y = 7 ::   97 <  128 < 128
// y = 6 ::   65 <   90 <  96
// y = 5 ::   33 <   60 <  64
// y = 4 ::    1 <   30 <  32
// y = 3 ::  -32 <  -30 <  -1
// y = 2 ::  -64 <  -60 < -33
// y = 1 ::  -96 <  -90 < -65
// y = 0 :: -128 < -128 < -97

// non-patch cable:
// y = 7 :: 113 < 128 < 128
// y = 6 ::  97 < 110 < 112
// y = 5 ::  81 <  91 <  96
// y = 4 ::  65 <  73 <  80
// y = 3 ::  49 <  55 <  64
// y = 2 ::  33 <  37 <  48
// y = 1 ::  17 <  18 <  32
// y = 0 ::  0  <   0 <  16

PLACE_SDRAM_BSS AutomationEditorLayoutModControllable automationEditorLayoutModControllable{};

// gets the length of the clip, renders the pads corresponding to current parameter values set up to the
// clip length renders the undefined area of the clip that the user can't interact with
void AutomationEditorLayoutModControllable::renderAutomationEditor(
    ModelStackWithAutoParam* modelStackWithParam, Clip* clip, RGB image[][kDisplayWidth + kSideBarWidth],
    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
    int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea, params::Kind kind, bool isBipolar) {
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		renderAutomationColumn(modelStackWithParam, image, occupancyMask, effectiveLength, xDisplay,
		                       modelStackWithParam->autoParam->isAutomated(), xScroll, xZoom, kind, isBipolar);
	}
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth,
		                    getAutomationView()->toTimelineView(), currentSong->tripletsOn, xDisplay);
	}
}

/// render each square in each column of the automation editor grid
void AutomationEditorLayoutModControllable::renderAutomationColumn(
    ModelStackWithAutoParam* modelStackWithParam, RGB image[][kDisplayWidth + kSideBarWidth],
    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t lengthToDisplay, int32_t xDisplay, bool isAutomated,
    int32_t xScroll, int32_t xZoom, params::Kind kind, bool isBipolar) {

	uint32_t squareStart = getMiddlePosFromSquare(xDisplay, lengthToDisplay, xScroll, xZoom);
	int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (isBipolar) {
			renderAutomationBipolarSquare(image, occupancyMask, xDisplay, yDisplay, isAutomated, kind, knobPos);
		}
		else {
			renderAutomationUnipolarSquare(image, occupancyMask, xDisplay, yDisplay, isAutomated, knobPos);
		}
	}
}

/// render column for bipolar params - e.g. pan, pitch, patch cable
void AutomationEditorLayoutModControllable::renderAutomationBipolarSquare(
    RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
    int32_t xDisplay, int32_t yDisplay, bool isAutomated, params::Kind kind, int32_t knobPos) {
	RGB& pixel = image[yDisplay][xDisplay];

	int32_t middleKnobPos;

	// for patch cable that has a range of -128 to + 128, the middle point is 0
	if (kind == params::Kind::PATCH_CABLE) {
		middleKnobPos = 0;
	}
	// for non-patch cable that has a range of 0 to 128, the middle point is 64
	else {
		middleKnobPos = 64;
	}

	// if it's bipolar, only render grid rows above or below middle value
	if (((knobPos > middleKnobPos) && (yDisplay < 4)) || ((knobPos < middleKnobPos) && (yDisplay > 3))) {
		pixel = colours::black; // erase pad
		return;
	}

	bool doRender = false;

	// determine whether or not you should render a row based on current value
	if (knobPos != middleKnobPos) {
		if (kind == params::Kind::PATCH_CABLE) {
			if (knobPos > middleKnobPos) {
				doRender = (knobPos >= patchCableMinPadDisplayValues[yDisplay]);
			}
			else {
				doRender = (knobPos <= patchCableMaxPadDisplayValues[yDisplay]);
			}
		}
		else {
			if (knobPos > middleKnobPos) {
				doRender = (knobPos >= nonPatchCableMinPadDisplayValues[yDisplay]);
			}
			else {
				doRender = (knobPos <= nonPatchCableMaxPadDisplayValues[yDisplay]);
			}
		}
	}

	// render automation lane
	if (doRender) {
		if (isAutomated) { // automated, render bright colour
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownColour[yDisplay];
			}
		}
		else { // not automated, render less bright tail colour
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownTailColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownTailColour[yDisplay];
			}
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
	else {
		pixel = colours::black; // erase pad
	}

	// pad selection mode, render cursor
	if (getPadSelectionOn() && ((xDisplay == getLeftPadSelectedX()) || (xDisplay == getRightPadSelectedX()))) {
		if (doRender) {
			if (knobPos > middleKnobPos) {
				pixel = rowBipolarDownBlurColour[-yDisplay + 7];
			}
			else {
				pixel = rowBipolarDownBlurColour[yDisplay];
			}
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

/// render column for unipolar params (e.g. not pan, pitch, or patch cables)
void AutomationEditorLayoutModControllable::renderAutomationUnipolarSquare(
    RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
    int32_t xDisplay, int32_t yDisplay, bool isAutomated, int32_t knobPos) {
	RGB& pixel = image[yDisplay][xDisplay];

	// determine whether or not you should render a row based on current value
	bool doRender = false;
	if (knobPos) {
		doRender = (knobPos >= nonPatchCableMinPadDisplayValues[yDisplay]);
	}

	// render square
	if (doRender) {
		if (isAutomated) { // automated, render bright colour
			pixel = rowColour[yDisplay];
		}
		else { // not automated, render less bright tail colour
			pixel = rowTailColour[yDisplay];
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
	else {
		pixel = colours::black; // erase pad
	}

	// pad selection mode, render cursor
	if (getPadSelectionOn() && ((xDisplay == getLeftPadSelectedX()) || (xDisplay == getRightPadSelectedX()))) {
		if (doRender) {
			pixel = rowBlurColour[yDisplay];
		}
		else {
			pixel = colours::grey;
		}
		occupancyMask[yDisplay][xDisplay] = 64;
	}
}

void AutomationEditorLayoutModControllable::renderAutomationEditorDisplayOLED(
    deluge::hid::display::oled_canvas::Canvas& canvas, Clip* clip, OutputType outputType, int32_t knobPosLeft,
    int32_t knobPosRight) {
	// display parameter name
	DEF_STACK_STRING_BUF(parameterName, 30);
	getAutomationParameterName(clip, outputType, parameterName);

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	canvas.drawStringCentredShrinkIfNecessary(parameterName.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	// display automation status
	yPos = yPos + 12;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (getOnArrangerView()) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
	}

	char const* isAutomated;

	// check if Parameter is currently automated so that the automation status can be drawn on
	// the screen with the Parameter Name
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		if (modelStackWithParam->autoParam->isAutomated()) {
			isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_ON);
		}
		else {
			isAutomated = l10n::get(l10n::String::STRING_FOR_AUTOMATION_OFF);
		}
	}

	canvas.drawStringCentred(isAutomated, yPos, kTextSpacingX, kTextSpacingY);

	// display parameter value
	yPos = yPos + 12;

	if (knobPosRight != kNoSelection) {
		char bufferLeft[10];
		bufferLeft[0] = 'L';
		bufferLeft[1] = ':';
		bufferLeft[2] = ' ';
		intToString(knobPosLeft, &bufferLeft[3]);
		canvas.drawString(bufferLeft, 0, yPos, kTextSpacingX, kTextSpacingY);

		char bufferRight[10];
		bufferRight[0] = 'R';
		bufferRight[1] = ':';
		bufferRight[2] = ' ';
		intToString(knobPosRight, &bufferRight[3]);
		canvas.drawStringAlignRight(bufferRight, yPos, kTextSpacingX, kTextSpacingY);
	}
	else {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
	}
}

void AutomationEditorLayoutModControllable::renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType,
                                                                              int32_t knobPosLeft,
                                                                              bool modEncoderAction) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (getOnArrangerView()) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

		modelStackWithParam =
		    currentSong->getModelStackWithParam(modelStackWithThreeMainThings, currentSong->lastSelectedParamID);
	}
	else {
		modelStackWithParam = getModelStackWithParamForClip(modelStack, clip);
	}

	bool padSelected = (!getPadSelectionOn() && isUIModeActive(UI_MODE_NOTES_PRESSED)) || getPadSelectionOn();

	/* check if you're holding a pad
	 * if yes, store pad press knob position in getLastPadSelectedKnobPos()
	 * so that it can be used next time as the knob position if returning here
	 * to display parameter value after another popup has been cancelled (e.g. audition pad)
	 */
	if (padSelected) {
		if (knobPosLeft != kNoSelection) {
			getLastPadSelectedKnobPos() = knobPosLeft;
		}
		else if (getLastPadSelectedKnobPos() != kNoSelection) {
			params::Kind lastSelectedParamKind = params::Kind::NONE;
			int32_t lastSelectedParamID = kNoSelection;
			if (getOnArrangerView()) {
				lastSelectedParamKind = currentSong->lastSelectedParamKind;
				lastSelectedParamID = currentSong->lastSelectedParamID;
			}
			else {
				lastSelectedParamKind = clip->lastSelectedParamKind;
				lastSelectedParamID = clip->lastSelectedParamID;
			}
			knobPosLeft = view.calculateKnobPosForDisplay(lastSelectedParamKind, lastSelectedParamID,
			                                              getLastPadSelectedKnobPos());
		}
	}

	bool isAutomated =
	    modelStackWithParam && modelStackWithParam->autoParam && modelStackWithParam->autoParam->isAutomated();
	bool playbackStarted = playbackHandler.isEitherClockActive();

	// display parameter value if knobPos is provided
	if ((knobPosLeft != kNoSelection) && (padSelected || (playbackStarted && isAutomated) || modEncoderAction)) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		if (modEncoderAction && !padSelected) {
			display->displayPopup(buffer, 3, true);
		}
		else {
			display->setText(buffer, true, 255, false);
		}
	}
	// display parameter name
	else if (knobPosLeft == kNoSelection) {
		DEF_STACK_STRING_BUF(parameterName, 30);
		getAutomationParameterName(clip, outputType, parameterName);
		// if playback is running and there is automation, the screen will display the
		// current automation value at the playhead position
		// when changing to a parameter with automation, flash the parameter name first
		// before the value is displayed
		// otherwise if there's no automation, just scroll the parameter name
		if (padSelected || (playbackStarted && isAutomated)) {
			display->displayPopup(parameterName.c_str(), 3, true, isAutomated ? 3 : 255);
		}
		else {
			display->setScrollingText(parameterName.c_str(), 0, 600, -1, isAutomated ? 3 : 255);
		}
	}
}

// get's the name of the Parameter being edited so it can be displayed on the screen
void AutomationEditorLayoutModControllable::getAutomationParameterName(Clip* clip, OutputType outputType,
                                                                       StringBuf& parameterName) {
	if (getOnArrangerView() || outputType != OutputType::MIDI_OUT) {
		params::Kind lastSelectedParamKind = params::Kind::NONE;
		int32_t lastSelectedParamID = kNoSelection;
		PatchSource lastSelectedPatchSource = PatchSource::NONE;
		if (getOnArrangerView()) {
			lastSelectedParamKind = currentSong->lastSelectedParamKind;
			lastSelectedParamID = currentSong->lastSelectedParamID;
		}
		else {
			lastSelectedParamKind = clip->lastSelectedParamKind;
			lastSelectedParamID = clip->lastSelectedParamID;
			lastSelectedPatchSource = clip->lastSelectedPatchSource;
		}
		if (lastSelectedParamKind == params::Kind::PATCH_CABLE) {
			PatchSource source2 = PatchSource::NONE;
			ParamDescriptor paramDescriptor;
			paramDescriptor.data = lastSelectedParamID;
			if (!paramDescriptor.hasJustOneSource()) {
				source2 = paramDescriptor.getTopLevelSource();
			}

			parameterName.append(sourceToStringShort(lastSelectedPatchSource));

			if (display->haveOLED()) {
				parameterName.append(" -> ");
			}
			else {
				parameterName.append(" - ");
			}

			if (source2 != PatchSource::NONE) {
				parameterName.append(sourceToStringShort(source2));
				parameterName.append(display->haveOLED() ? " -> " : " - ");
			}

			parameterName.append(params::getPatchedParamShortName(lastSelectedParamID));
		}
		else {
			parameterName.append(getParamDisplayName(lastSelectedParamKind, lastSelectedParamID));
		}
	}
	else {
		if (clip->lastSelectedParamID == CC_NUMBER_NONE) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_PITCH_BEND) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
		}
		else if (clip->lastSelectedParamID == CC_NUMBER_AFTERTOUCH) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
		}
		else if (clip->lastSelectedParamID == CC_EXTERNAL_MOD_WHEEL || clip->lastSelectedParamID == CC_NUMBER_Y_AXIS) {
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
		}
		else {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)clip->output;
			bool appendedName = false;

			if (clip->lastSelectedParamID >= 0 && clip->lastSelectedParamID < kNumRealCCNumbers) {
				std::string_view name = midiInstrument->getNameFromCC(clip->lastSelectedParamID);
				// if we have a name for this midi cc set by the user, display that instead of the cc number
				if (!name.empty()) {
					parameterName.append(name.data());
					appendedName = true;
				}
			}

			// if we don't have a midi cc name set, draw CC number instead
			if (!appendedName) {
				if (display->haveOLED()) {
					parameterName.append("CC ");
					parameterName.appendInt(clip->lastSelectedParamID);
				}
				else {
					if (clip->lastSelectedParamID < 100) {
						parameterName.append("CC");
					}
					else {
						parameterName.append("C");
					}
					parameterName.appendInt(clip->lastSelectedParamID);
				}
			}
		}
	}
}

/// toggle automation interpolation on / off
bool AutomationEditorLayoutModControllable::toggleAutomationInterpolation() {
	if (getInterpolation()) {
		getInterpolation() = false;
		initInterpolation();
		resetInterpolationShortcutBlinking();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_DISABLED));
	}
	else {
		getInterpolation() = true;
		blinkInterpolationShortcut();

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_ENABLED));
	}
	return true;
}

/// toggle automation pad selection mode on / off
bool AutomationEditorLayoutModControllable::toggleAutomationPadSelectionMode(
    ModelStackWithAutoParam* modelStackWithParam, int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	// enter/exit pad selection mode
	if (getPadSelectionOn()) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_OFF));

		initPadSelection();
		displayAutomation(true, !display->have7SEG());
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PAD_SELECTION_ON));

		getPadSelectionOn() = true;
		blinkPadSelectionShortcut();

		getMultiPadPressSelected() = false;
		getMultiPadPressActive() = false;

		// display only left cursor initially
		getLeftPadSelectedX() = 0;
		getRightPadSelectedX() = kNoSelection;

		uint32_t squareStart = getMiddlePosFromSquare(getLeftPadSelectedX(), effectiveLength, xScroll, xZoom);

		updateAutomationModPosition(modelStackWithParam, squareStart, true, true);
	}
	uiNeedsRendering(getAutomationView());
	return true;
}

// automation edit pad action
// handles single and multi pad presses for automation editing
// stores pad presses in the EditPadPresses struct of the instrument clip view
void AutomationEditorLayoutModControllable::automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam,
                                                                    Clip* clip, int32_t xDisplay, int32_t yDisplay,
                                                                    int32_t velocity, int32_t effectiveLength,
                                                                    int32_t xScroll, int32_t xZoom) {
	// If button down
	if (velocity) {
		// If this is a automation-length-edit press...
		// needed for Automation
		if (instrumentClipView.numEditPadPresses == 1) {

			int32_t firstPadX = 255;
			int32_t firstPadY = 255;

			// Find that original press
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {

					firstPadX = instrumentClipView.editPadPresses[i].xDisplay;
					firstPadY = instrumentClipView.editPadPresses[i].yDisplay;

					break;
				}
			}

			if (firstPadX != 255 && firstPadY != 255) {
				if (firstPadX != xDisplay) {
					recordAutomationSinglePadPress(xDisplay, yDisplay);

					getMultiPadPressSelected() = true;
					getMultiPadPressActive() = true;

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					getLeftPadSelectedX() = firstPadX > xDisplay ? xDisplay : firstPadX;
					getLeftPadSelectedY() = firstPadX > xDisplay ? yDisplay : firstPadY;
					getRightPadSelectedX() = firstPadX > xDisplay ? firstPadX : xDisplay;
					getRightPadSelectedY() = firstPadX > xDisplay ? firstPadY : yDisplay;

					// if you're not in pad selection mode, allow user to enter a long press
					if (!getPadSelectionOn()) {
						handleAutomationMultiPadPress(modelStackWithParam, clip, getLeftPadSelectedX(),
						                              getLeftPadSelectedY(), getRightPadSelectedX(),
						                              getRightPadSelectedY(), effectiveLength, xScroll, xZoom);
					}
					else {
						uiNeedsRendering(getAutomationView());
					}

					// set led indicators to left / right pad selection values
					// and update display
					renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
					                                        xDisplay);
				}
				else {
					getLeftPadSelectedY() = firstPadY;
					getMiddlePadPressSelected() = true;
					goto singlePadPressAction;
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {
singlePadPressAction:
			if (recordAutomationSinglePadPress(xDisplay, yDisplay)) {
				getMultiPadPressActive() = false;
				handleAutomationSinglePadPress(modelStackWithParam, clip, xDisplay, yDisplay, effectiveLength, xScroll,
				                               xZoom);
			}
		}
	}

	// Or if pad press ended...
	else {
		// Find the corresponding press, if there is one
		int32_t i;
		for (i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive
			    && instrumentClipView.editPadPresses[i].yDisplay == yDisplay
			    && instrumentClipView.editPadPresses[i].xDisplay == xDisplay) {
				break;
			}
		}

		// If we found it...
		if (i < kEditPadPressBufferSize) {
			instrumentClipView.endEditPadPress(i);

			instrumentClipView.checkIfAllEditPadPressesEnded();
		}

		// outside pad selection mode, exit multi pad press once you've let go of the first pad in the
		// long press
		if (!getPadSelectionOn() && getMultiPadPressSelected() && (currentUIMode != UI_MODE_NOTES_PRESSED)) {
			initPadSelection();
		}
		// switch from long press selection to short press selection in pad selection mode
		else if (getPadSelectionOn() && getMultiPadPressSelected() && !getMultiPadPressActive()
		         && (currentUIMode != UI_MODE_NOTES_PRESSED)
		         && ((AudioEngine::audioSampleTimer - instrumentClipView.timeLastEditPadPress) < kShortPressTime)) {

			getMultiPadPressSelected() = false;

			getLeftPadSelectedX() = xDisplay;
			getRightPadSelectedX() = kNoSelection;

			uiNeedsRendering(getAutomationView());
		}

		if (currentUIMode != UI_MODE_NOTES_PRESSED) {
			getLastPadSelectedKnobPos() = kNoSelection;
			if (getMultiPadPressSelected()) {
				renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
				                                        xDisplay);
			}
			else if (!getPadSelectionOn() && !playbackHandler.isEitherClockActive()) {
				displayAutomation();
			}
		}

		getMiddlePadPressSelected() = false;
	}
}

bool AutomationEditorLayoutModControllable::recordAutomationSinglePadPress(int32_t xDisplay, int32_t yDisplay) {
	instrumentClipView.timeLastEditPadPress = AudioEngine::audioSampleTimer;
	// Find an empty space in the press buffer, if there is one
	int32_t i;
	for (i = 0; i < kEditPadPressBufferSize; i++) {
		if (!instrumentClipView.editPadPresses[i].isActive) {
			break;
		}
	}
	if (i < kEditPadPressBufferSize) {
		instrumentClipView.shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

		// If this is the first press, record the time
		if (instrumentClipView.numEditPadPresses == 0) {
			instrumentClipView.timeFirstEditPadPress = AudioEngine::audioSampleTimer;
			instrumentClipView.shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
		}

		instrumentClipView.editPadPresses[i].isActive = true;
		instrumentClipView.editPadPresses[i].yDisplay = yDisplay;
		instrumentClipView.editPadPresses[i].xDisplay = xDisplay;
		instrumentClipView.numEditPadPresses++;
		instrumentClipView.numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
		enterUIMode(UI_MODE_NOTES_PRESSED);

		return true;
	}
	return false;
}

bool AutomationEditorLayoutModControllable::automationModEncoderActionForSelectedPad(
    ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder, int32_t offset, int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		int32_t xDisplay = 0;

		// for a multi pad press, adjust value of first or last pad depending on mod encoder turned
		if (getMultiPadPressSelected()) {
			if (whichModEncoder == 0) {
				xDisplay = getLeftPadSelectedX();
			}
			else if (whichModEncoder == 1) {
				xDisplay = getRightPadSelectedX();
			}
		}

		// if not multi pad press, but in pad selection mode, then just adjust the single selected pad
		else if (getPadSelectionOn()) {
			xDisplay = getLeftPadSelectedX();
		}

		// otherwise if not in pad selection mode, adjust the value of the pad currently being held
		else {
			// find pads that are currently pressed
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {
					xDisplay = instrumentClipView.editPadPresses[i].xDisplay;
				}
			}
		}

		uint32_t squareStart = 0;

		int32_t xScroll = currentSong->xScroll[getNavSysId()];
		int32_t xZoom = currentSong->xZoom[getNavSysId()];

		// for the second pad pressed in a long press, the square start position is set to the very last
		// nodes position
		if (getMultiPadPressSelected() && (whichModEncoder == 1)) {
			int32_t squareRightEdge = getPosFromSquare(xDisplay + 1, xScroll, xZoom);
			squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
		}
		else {
			squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
		}

		if (squareStart < effectiveLength) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, squareStart);

			int32_t newKnobPos = calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!getOnArrangerView() && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return true;
			}

			// use default interpolation settings
			initInterpolation();

			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom, true);

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// once first or last pad in a multi pad press is adjusted, re-render calculate multi pad
			// press based on revised start/ending values
			if (getMultiPadPressSelected()) {

				handleAutomationMultiPadPress(modelStackWithParam, clip, getLeftPadSelectedX(), 0,
				                              getRightPadSelectedX(), 0, effectiveLength, xScroll, xZoom, true);

				renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom,
				                                        xDisplay, true);

				return true;
			}
		}
	}

	return false;
}

void AutomationEditorLayoutModControllable::automationModEncoderActionForUnselectedPad(
    ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder, int32_t offset, int32_t effectiveLength) {
	Clip* clip = getCurrentClip();

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		if (modelStackWithParam->getTimelineCounter()
		    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

			int32_t knobPos = getAutomationParameterKnobPos(modelStackWithParam, view.modPos);

			int32_t newKnobPos = calculateAutomationKnobPosForModEncoderTurn(modelStackWithParam, knobPos, offset);

			// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
			// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a
			// value change gets recorded if newKnobPos exceeds 127, then it means current knobPos was
			// 127 and it was increased to 128. In which case, ignore value change
			if (!getOnArrangerView() && ((clip->output->type == OutputType::MIDI_OUT) && (newKnobPos == 64))) {
				return;
			}

			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

			// use default interpolation settings
			initInterpolation();

			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
			                                                          view.modLength);

			if (!getOnArrangerView()) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}

			if (!playbackHandler.isEitherClockActive() || !modelStackWithParam->autoParam->isAutomated()) {
				int32_t knobPos = newKnobPos + kKnobPosOffset;
				renderDisplay(knobPos, kNoSelection, true);
				setAutomationKnobIndicatorLevels(modelStackWithParam, knobPos, knobPos);
			}

			view.potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

			// midi follow and midi feedback enabled
			// re-send midi cc because learned parameter value has changed
			view.sendMidiFollowFeedback(modelStackWithParam, newKnobPos);
		}
	}
}

void AutomationEditorLayoutModControllable::copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                           int32_t xScroll, int32_t xZoom) {
	if (getCopiedParamAutomation()->nodes) {
		delugeDealloc(getCopiedParamAutomation()->nodes);
		getCopiedParamAutomation()->nodes = nullptr;
		getCopiedParamAutomation()->numNodes = 0;
	}

	int32_t startPos = getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = getPosFromSquare(kDisplayWidth, xScroll, xZoom);
	if (startPos == endPos) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->copy(startPos, endPos, getCopiedParamAutomation(), isPatchCable,
		                                     modelStackWithParam);

		if (getCopiedParamAutomation()->nodes) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_COPIED));
			return;
		}
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_COPY));
}

void AutomationEditorLayoutModControllable::pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
                                                            int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (!getCopiedParamAutomation()->nodes) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NO_AUTOMATION_TO_PASTE));
		return;
	}

	int32_t startPos = getPosFromSquare(0, xScroll, xZoom);
	int32_t endPos = getPosFromSquare(kDisplayWidth, xScroll, xZoom);

	int32_t pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / getCopiedParamAutomation()->width;

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);

		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithParam, false);
		}

		bool isPatchCable = (modelStackWithParam->paramCollection
		                     == modelStackWithParam->paramManager->getPatchCableSetAllowJibberish());
		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.

		modelStackWithParam->autoParam->paste(startPos, endPos, scaleFactor, modelStackWithParam,
		                                      getCopiedParamAutomation(), isPatchCable);

		display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_PASTED));

		if (playbackHandler.isEitherClockActive()) {
			currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
		}
		else {
			if (getPadSelectionOn()) {
				if (getMultiPadPressSelected()) {
					renderAutomationDisplayForMultiPadPress(modelStackWithParam, clip, effectiveLength, xScroll, xZoom);
				}
				else {
					uint32_t squareStart =
					    getMiddlePosFromSquare(getLeftPadSelectedX(), effectiveLength, xScroll, xZoom);

					updateAutomationModPosition(modelStackWithParam, squareStart);
				}
			}
			else {
				displayAutomation();
			}
		}

		return;
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_PASTE_AUTOMATION));
}

uint32_t AutomationEditorLayoutModControllable::getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll,
                                                               int32_t xZoom) {
	int32_t squareRightEdge = getPosFromSquare(square + 1, xScroll, xZoom);
	return std::min(effectiveLength, squareRightEdge) - getPosFromSquare(square, xScroll, xZoom);
}

// when pressing on a single pad, you want to display the value of the middle node within that square
// as that is the most accurate value that represents that square
uint32_t AutomationEditorLayoutModControllable::getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength,
                                                                       int32_t xScroll, int32_t xZoom) {
	uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
	uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	if (squareWidth != 3) {
		squareStart = squareStart + (squareWidth / 2);
	}

	return squareStart;
}

// this function obtains a parameters value and converts it to a knobPos
// the knobPos is used for rendering the current parameter values in the automation editor
// it's also used for obtaining the start and end position values for a multi pad press
// and also used for increasing/decreasing parameter values with the mod encoders
int32_t AutomationEditorLayoutModControllable::getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack,
                                                                             uint32_t squareStart) {
	// obtain value corresponding to the two pads that were pressed in a multi pad press action
	int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareStart, modelStack);
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(currentValue, modelStack);

	return knobPos;
}

// this function is based off the code in AutoParam::getValueAtPos, it was tweaked to just return
// interpolation status of the left node or right node (depending on the reversed parameter which is
// used to indicate what node in what direction we are looking for (e.g. we want status of left node, or
// right node, relative to the current pos we are looking at
bool AutomationEditorLayoutModControllable::getAutomationNodeInterpolation(ModelStackWithAutoParam* modelStack,
                                                                           int32_t pos, bool reversed) {

	if (!modelStack->autoParam->nodes.getNumElements()) {
		return false;
	}

	int32_t rightI = modelStack->autoParam->nodes.search(pos + (int32_t)!reversed, GREATER_OR_EQUAL);
	if (rightI >= modelStack->autoParam->nodes.getNumElements()) {
		rightI = 0;
	}
	ParamNode* rightNode = modelStack->autoParam->nodes.getElement(rightI);

	int32_t leftI = rightI - 1;
	if (leftI < 0) {
		leftI += modelStack->autoParam->nodes.getNumElements();
	}
	ParamNode* leftNode = modelStack->autoParam->nodes.getElement(leftI);

	if (reversed) {
		return leftNode->interpolated;
	}
	else {
		return rightNode->interpolated;
	}
}

// this function writes the new values calculated by the handleAutomationSinglePadPress and
// handleAutomationMultiPadPress functions
void AutomationEditorLayoutModControllable::setAutomationParameterValue(ModelStackWithAutoParam* modelStack,
                                                                        int32_t knobPos, int32_t squareStart,
                                                                        int32_t xDisplay, int32_t effectiveLength,
                                                                        int32_t xScroll, int32_t xZoom,
                                                                        bool modEncoderAction) {

	int32_t newValue = modelStack->paramCollection->knobPosToParamValue(knobPos, modelStack);

	uint32_t squareWidth = 0;

	// for a multi pad press, the beginning and ending pad presses are set with a square width of 3 (1
	// node).
	if (getMultiPadPressSelected()) {
		squareWidth = kParamNodeWidth;
	}
	else {
		squareWidth = getSquareWidth(xDisplay, effectiveLength, xScroll, xZoom);
	}

	// if you're doing a single pad press, you don't want the values around that single press position
	// to change they will change if those nodes around the single pad press were created with
	// interpolation turned on to fix this, re-create those nodes with their current value with
	// interpolation off

	getInterpolationBefore() = getAutomationNodeInterpolation(modelStack, squareStart, true);
	getInterpolationAfter() = getAutomationNodeInterpolation(modelStack, squareStart, false);

	// create a node to the left with the current interpolation status
	int32_t squareNodeLeftStart = squareStart - kParamNodeWidth;
	if (squareNodeLeftStart >= 0) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareNodeLeftStart, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareNodeLeftStart,
		                                                 kParamNodeWidth);
	}

	// create a node to the right with the current interpolation status
	int32_t squareNodeRightStart = squareStart + kParamNodeWidth;
	if (squareNodeRightStart < effectiveLength) {
		int32_t currentValue = modelStack->autoParam->getValuePossiblyAtPos(squareNodeRightStart, modelStack);
		modelStack->autoParam->setValuePossiblyForRegion(currentValue, modelStack, squareNodeRightStart,
		                                                 kParamNodeWidth);
	}

	// reset interpolation to false for the single pad we're changing (so that the nodes around it don't
	// also change)
	initInterpolation();

	// called twice because there was a weird bug where for some reason the first call wasn't taking
	// effect on one pad (and whatever pad it was changed every time)...super weird...calling twice
	// fixed it...
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);
	modelStack->autoParam->setValuePossiblyForRegion(newValue, modelStack, squareStart, squareWidth);

	if (!getOnArrangerView()) {
		modelStack->getTimelineCounter()->instrumentBeenEdited();
	}

	// in a multi pad press, no need to display all the values calculated
	if (!getMultiPadPressSelected()) {
		int32_t newKnobPos = knobPos + kKnobPosOffset;
		renderDisplay(newKnobPos, kNoSelection, modEncoderAction);
		setAutomationKnobIndicatorLevels(modelStack, newKnobPos, newKnobPos);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc because learned parameter value has changed
	view.sendMidiFollowFeedback(modelStack, knobPos);
}

// sets both knob indicators to the same value when pressing single pad,
// deleting automation, or displaying current parameter value
// multi pad presses don't use this function
void AutomationEditorLayoutModControllable::setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack,
                                                                             int32_t knobPosLeft,
                                                                             int32_t knobPosRight) {
	params::Kind kind = modelStack->paramCollection->getParamKind();
	bool isBipolar = isParamBipolar(kind, modelStack->paramId);

	// if you're dealing with a patch cable which has a -128 to +128 range
	// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
	if (kind == params::Kind::PATCH_CABLE) {
		knobPosLeft = view.convertPatchCableKnobPosToIndicatorLevel(knobPosLeft);
		knobPosRight = view.convertPatchCableKnobPosToIndicatorLevel(knobPosRight);
	}

	bool isBlinking = indicator_leds::isKnobIndicatorBlinking(0) || indicator_leds::isKnobIndicatorBlinking(1);

	if (!isBlinking) {
		indicator_leds::setKnobIndicatorLevel(0, knobPosLeft, isBipolar);
		indicator_leds::setKnobIndicatorLevel(1, knobPosRight, isBipolar);
	}
}

// updates the position that the active mod controllable stack is pointing to
// this sets the current value for the active parameter so that it can be auditioned
void AutomationEditorLayoutModControllable::updateAutomationModPosition(ModelStackWithAutoParam* modelStack,
                                                                        uint32_t squareStart, bool updateDisplay,
                                                                        bool updateIndicatorLevels) {

	if (!playbackHandler.isEitherClockActive() || getPadSelectionOn()) {
		if (modelStack && modelStack->autoParam) {
			if (modelStack->getTimelineCounter()
			    == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

				view.activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
				    squareStart, &view.activeModControllableModelStack);

				int32_t knobPos = getAutomationParameterKnobPos(modelStack, squareStart) + kKnobPosOffset;

				if (updateDisplay) {
					renderDisplay(knobPos);
				}

				if (updateIndicatorLevels) {
					setAutomationKnobIndicatorLevels(modelStack, knobPos, knobPos);
				}
			}
		}
	}
}

// takes care of setting the automation value for the single pad that was pressed
void AutomationEditorLayoutModControllable::handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam,
                                                                           Clip* clip, int32_t xDisplay,
                                                                           int32_t yDisplay, int32_t effectiveLength,
                                                                           int32_t xScroll, int32_t xZoom) {

	Output* output = clip->output;
	OutputType outputType = output->type;

	// this means you are editing a parameter's value
	handleAutomationParameterChange(modelStackWithParam, clip, outputType, xDisplay, yDisplay, effectiveLength, xScroll,
	                                xZoom);

	uiNeedsRendering(getAutomationView());
}

// called by handle single pad press when it is determined that you are editing parameter automation
// using the grid
void AutomationEditorLayoutModControllable::handleAutomationParameterChange(
    ModelStackWithAutoParam* modelStackWithParam, Clip* clip, OutputType outputType, int32_t xDisplay, int32_t yDisplay,
    int32_t effectiveLength, int32_t xScroll, int32_t xZoom) {
	if (getPadSelectionOn()) {
		// display pad's value
		uint32_t squareStart = 0;

		// if a long press is selected and you're checking value of start or end pad
		// display value at very first or very last node
		if (getMultiPadPressSelected()
		    && ((getLeftPadSelectedX() == xDisplay) || (getRightPadSelectedX() == xDisplay))) {
			if (getLeftPadSelectedX() == xDisplay) {
				squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);
			}
			else {
				int32_t squareRightEdge = getPosFromSquare(getRightPadSelectedX() + 1, xScroll, xZoom);
				squareStart = std::min(effectiveLength, squareRightEdge) - kParamNodeWidth;
			}
		}
		// display pad's middle value
		else {
			squareStart = getMiddlePosFromSquare(xDisplay, effectiveLength, xScroll, xZoom);
		}

		updateAutomationModPosition(modelStackWithParam, squareStart);

		if (!getMultiPadPressSelected()) {
			getLeftPadSelectedX() = xDisplay;
		}
	}

	else if (modelStackWithParam && modelStackWithParam->autoParam) {

		uint32_t squareStart = getPosFromSquare(xDisplay, xScroll, xZoom);

		if (squareStart < effectiveLength) {
			// use default interpolation settings
			initInterpolation();

			int32_t newKnobPos = calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, yDisplay);
			setAutomationParameterValue(modelStackWithParam, newKnobPos, squareStart, xDisplay, effectiveLength,
			                            xScroll, xZoom);
		}
	}
}

int32_t AutomationEditorLayoutModControllable::calculateAutomationKnobPosForPadPress(
    ModelStackWithAutoParam* modelStackWithParam, OutputType outputType, int32_t yDisplay) {

	int32_t newKnobPos = 0;
	params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

	if (getMiddlePadPressSelected()) {
		newKnobPos = calculateAutomationKnobPosForMiddlePadPress(kind, yDisplay);
	}
	else {
		newKnobPos = calculateAutomationKnobPosForSinglePadPress(kind, yDisplay);
	}

	// for Midi Clips, maxKnobPos = 127
	if (outputType == OutputType::MIDI_OUT && newKnobPos == kMaxKnobPos) {
		newKnobPos -= 1; // 128 - 1 = 127
	}

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a second pad in the same column
// middle value is calculated by taking average of min and max value of the range for the two pad
// presses
int32_t AutomationEditorLayoutModControllable::calculateAutomationKnobPosForMiddlePadPress(params::Kind kind,
                                                                                           int32_t yDisplay) {
	int32_t newKnobPos = 0;

	int32_t yMin = yDisplay < getLeftPadSelectedY() ? yDisplay : getLeftPadSelectedY();
	int32_t yMax = yDisplay > getLeftPadSelectedY() ? yDisplay : getLeftPadSelectedY();
	int32_t minKnobPos = 0;
	int32_t maxKnobPos = 0;

	if (kind == params::Kind::PATCH_CABLE) {
		minKnobPos = patchCableMinPadDisplayValues[yMin];
		maxKnobPos = patchCableMaxPadDisplayValues[yMax];
	}
	else {
		minKnobPos = nonPatchCableMinPadDisplayValues[yMin];
		maxKnobPos = nonPatchCableMaxPadDisplayValues[yMax];
	}

	newKnobPos = (minKnobPos + maxKnobPos) >> 1;

	return newKnobPos;
}

// calculates what the new parameter value is when you press a single pad
int32_t AutomationEditorLayoutModControllable::calculateAutomationKnobPosForSinglePadPress(params::Kind kind,
                                                                                           int32_t yDisplay) {
	int32_t newKnobPos = 0;

	// patch cable
	if (kind == params::Kind::PATCH_CABLE) {
		newKnobPos = patchCablePadPressValues[yDisplay];
	}
	// non patch cable
	else {
		newKnobPos = nonPatchCablePadPressValues[yDisplay];
	}

	return newKnobPos;
}

// takes care of setting the automation values for the two pads pressed and the pads in between
void AutomationEditorLayoutModControllable::handleAutomationMultiPadPress(
    ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t firstPadX, int32_t firstPadY, int32_t secondPadX,
    int32_t secondPadY, int32_t effectiveLength, int32_t xScroll, int32_t xZoom, bool modEncoderAction) {

	int32_t secondPadLeftEdge = getPosFromSquare(secondPadX, xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = getPosFromSquare(firstPadX, xScroll, xZoom);
		int32_t secondPadRightEdge = getPosFromSquare(secondPadX + 1, xScroll, xZoom);

		int32_t firstPadValue = 0;
		int32_t secondPadValue = 0;

		// if we're updating the long press values via mod encoder action, then get current values of
		// pads pressed and re-interpolate
		if (modEncoderAction) {
			firstPadValue = getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

			uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;

			secondPadValue = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;
		}

		// otherwise if it's a regular long press, calculate values from the y position of the pads
		// pressed
		else {
			OutputType outputType = clip->output->type;
			firstPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, firstPadY) + kKnobPosOffset;
			secondPadValue =
			    calculateAutomationKnobPosForPadPress(modelStackWithParam, outputType, secondPadY) + kKnobPosOffset;
		}

		// clear existing nodes from long press range

		// reset interpolation settings to default
		initInterpolation();

		// set value for beginning pad press at the very first node position within that pad
		setAutomationParameterValue(modelStackWithParam, firstPadValue - kKnobPosOffset, firstPadLeftEdge, firstPadX,
		                            effectiveLength, xScroll, xZoom);

		// set value for ending pad press at the very last node position within that pad
		int32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		setAutomationParameterValue(modelStackWithParam, secondPadValue - kKnobPosOffset, squareStart, secondPadX,
		                            effectiveLength, xScroll, xZoom);

		// converting variables to float for more accurate interpolation calculation
		float firstPadValueFloat = static_cast<float>(firstPadValue);
		float firstPadXFloat = static_cast<float>(firstPadLeftEdge);
		float secondPadValueFloat = static_cast<float>(secondPadValue);
		float secondPadXFloat = static_cast<float>(squareStart);

		// loop from first pad to last pad, setting values for nodes in between
		// these values will serve as "key frames" for the interpolation to flow through
		for (int32_t x = firstPadX; x <= secondPadX; x++) {

			int32_t newKnobPos = 0;
			uint32_t squareWidth = 0;

			// we've already set the value for the very first node corresponding to the first pad above
			// now we will set the value for the remaining nodes within the first pad
			if (x == firstPadX) {
				squareStart = getPosFromSquare(x, xScroll, xZoom) + kParamNodeWidth;
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// we've already set the value for the very last node corresponding to the second pad above
			// now we will set the value for the remaining nodes within the second pad
			else if (x == secondPadX) {
				squareStart = getPosFromSquare(x, xScroll, xZoom);
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom) - kParamNodeWidth;
			}

			// now we will set the values for the nodes between the first and second pad's pressed
			else {
				squareStart = getPosFromSquare(x, xScroll, xZoom);
				squareWidth = getSquareWidth(x, effectiveLength, xScroll, xZoom);
			}

			// linear interpolation formula to calculate the value of the pads
			// f(x) = A + (x - Ax) * ((B - A) / (Bx - Ax))
			float newKnobPosFloat = std::round(firstPadValueFloat
			                                   + (((squareStart - firstPadXFloat) / kParamNodeWidth)
			                                      * ((secondPadValueFloat - firstPadValueFloat)
			                                         / ((secondPadXFloat - firstPadXFloat) / kParamNodeWidth))));

			newKnobPos = static_cast<int32_t>(newKnobPosFloat);
			newKnobPos = newKnobPos - kKnobPosOffset;

			// if interpolation is off, values for nodes in between first and second pad will not be set
			// in a staggered/step fashion
			if (getInterpolation()) {
				getInterpolationBefore() = true;
				getInterpolationAfter() = true;
			}

			// set value for pads in between
			int32_t newValue =
			    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);
			modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, squareStart,
			                                                          squareWidth);

			if (!getOnArrangerView()) {
				modelStackWithParam->getTimelineCounter()->instrumentBeenEdited();
			}
		}

		// reset interpolation settings to off
		initInterpolation();

		// render the multi pad press
		uiNeedsRendering(getAutomationView());
	}
}

// new function to render display when a long press is active
// on OLED this will display the left and right position in a long press on the screen
// on 7SEG this will display the position of the last selected pad
// also updates LED indicators. bottom LED indicator = left pad, top LED indicator = right pad
void AutomationEditorLayoutModControllable::renderAutomationDisplayForMultiPadPress(
    ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
    int32_t xDisplay, bool modEncoderAction) {

	int32_t secondPadLeftEdge = getPosFromSquare(getRightPadSelectedX(), xScroll, xZoom);

	if (effectiveLength <= 0 || secondPadLeftEdge > effectiveLength) {
		return;
	}

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		int32_t firstPadLeftEdge = getPosFromSquare(getLeftPadSelectedX(), xScroll, xZoom);
		int32_t secondPadRightEdge = getPosFromSquare(getRightPadSelectedX() + 1, xScroll, xZoom);

		int32_t knobPosLeft = getAutomationParameterKnobPos(modelStackWithParam, firstPadLeftEdge) + kKnobPosOffset;

		uint32_t squareStart = std::min(effectiveLength, secondPadRightEdge) - kParamNodeWidth;
		int32_t knobPosRight = getAutomationParameterKnobPos(modelStackWithParam, squareStart) + kKnobPosOffset;

		if (xDisplay != kNoSelection) {
			if (getLeftPadSelectedX() == xDisplay) {
				squareStart = firstPadLeftEdge;
				getLastPadSelectedKnobPos() = knobPosLeft;
			}
			else {
				getLastPadSelectedKnobPos() = knobPosRight;
			}
		}

		if (display->haveOLED()) {
			renderDisplay(knobPosLeft, knobPosRight);
		}
		// display pad value of second pad pressed
		else {
			if (modEncoderAction) {
				renderDisplay(getLastPadSelectedKnobPos());
			}
			else {
				renderDisplay();
			}
		}

		setAutomationKnobIndicatorLevels(modelStackWithParam, knobPosLeft, knobPosRight);

		// update position of mod controllable stack
		updateAutomationModPosition(modelStackWithParam, squareStart, false, false);
	}
}

// used to calculate new knobPos when you turn the mod encoders (gold knobs)
int32_t AutomationEditorLayoutModControllable::calculateAutomationKnobPosForModEncoderTurn(
    ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos, int32_t offset) {

	// adjust the current knob so that it is within the range of 0-128 for calculation purposes
	knobPos = knobPos + kKnobPosOffset;

	int32_t newKnobPos = 0;

	if ((knobPos + offset) < 0) {
		params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		if (kind == params::Kind::PATCH_CABLE) {
			if ((knobPos + offset) >= -kMaxKnobPos) {
				newKnobPos = knobPos + offset;
			}
			else if ((knobPos + offset) < -kMaxKnobPos) {
				newKnobPos = -kMaxKnobPos;
			}
			else {
				newKnobPos = knobPos;
			}
		}
		else {
			newKnobPos = knobPos;
		}
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

	// in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos
	// set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

//}; // namespace deluge::gui::views::automation::editor_layout
