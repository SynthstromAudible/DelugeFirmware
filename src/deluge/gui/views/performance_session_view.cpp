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

#include "gui/views/performance_session_view.h"
#include "definitions_cxx.hpp"
#include "dsp/master_compressor/master_compressor.h"
#include "extern.h"
#include "gui/colour.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/instrument.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <algorithm>
#include <cstdint>
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge;
using namespace gui;

//kit affect entire FX - sorted in the order that Parameters are scrolled through on the display
const std::array<std::pair<Param::Kind, ParamType>, kDisplayWidth>
    songParamsForPerformance{{
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::PITCH_ADJUST},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::PITCH_ADJUST},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_FREQ}, //LPF Cutoff, Resonance
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_RES},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_FREQ}, //HPF Cutoff, Resonance
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_RES},
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS}, //Bass, Bass Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS_FREQ},
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE}, //Treble, Treble Freq
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE_FREQ},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT}, //Reverb Amount
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_RATE},         //Delay Rate, Amount
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_AMOUNT},
        {Param::Kind::UNPATCHED, Param::Unpatched::SAMPLE_RATE_REDUCTION}, //Decimation, Bitcrush
        {Param::Kind::UNPATCHED, Param::Unpatched::BITCRUSHING},
		{Param::Kind::UNPATCHED, Param::Unpatched::STUTTER_RATE},      //Stutter Rate
    }};

    //    {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_OFFSET}, //Mod FX Offset, Feedback, Depth, Rate
    //    {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_FEEDBACK},
    //    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH},
    //    {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},


//VU meter style colours for the automation editor

const uint8_t rowColour[kDisplayHeight][3] = {

    {0, 255, 0}, {36, 219, 0}, {73, 182, 0}, {109, 146, 0}, {146, 109, 0}, {182, 73, 0}, {219, 36, 0}, {255, 0, 0}};

const uint8_t rowTailColour[kDisplayHeight][3] = {

    {2, 53, 2}, {9, 46, 2}, {17, 38, 2}, {24, 31, 2}, {31, 24, 2}, {38, 17, 2}, {46, 9, 2}, {53, 2, 2}};

const uint8_t rowBlurColour[kDisplayHeight][3] = {

    {71, 111, 71}, {72, 101, 66}, {73, 90, 62}, {74, 80, 57}, {76, 70, 53}, {77, 60, 48}, {78, 49, 44}, {79, 39, 39}};

PerformanceSessionView performanceSessionView{};

PerformanceSessionView::PerformanceSessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		previousKnobPosition[xDisplay] = kNoSelection;
		currentKnobPosition[xDisplay] = kNoSelection;
	}
}

bool PerformanceSessionView::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	return false;
}

bool PerformanceSessionView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	focusRegained();

	currentSong->performanceView = true;

	return true;
}

void PerformanceSessionView::focusRegained() {
	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	selectedClipYDisplay = 255;
	if (display->haveOLED()) {
		setCentralLEDStates();
	}
	else {
		redrawNumericDisplay();
	}

	indicator_leds::setLedState(IndicatorLED::BACK, false);

	setLedStates();

	currentSong->lastClipInstanceEnteredStartPos = -1;
}

ActionResult PerformanceSessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	InstrumentType newInstrumentType;

	// Clip-view button
	if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE && playbackHandler.recording != RECORDING_ARRANGEMENT) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			sessionView.transitionToViewForClip(); // May fail if no currentClip
		}
	}

	// Song-view button without shift

	// Arranger view button, or if there isn't one then song view button
#ifdef arrangerViewButtonX
	else if (b == arrangerView) {
#else
	else if (b == SESSION_VIEW && !Buttons::isShiftButtonPressed()) {
#endif
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		bool lastSessionButtonActiveState = sessionButtonActive;
		sessionButtonActive = on;

		// Press with special modes
		if (on) {
			sessionButtonUsed = false;

			// If holding record button...
			if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
				Buttons::recordButtonPressUsedUp = true;

				// Make sure we weren't already playing...
				if (!playbackHandler.playbackState) {

					Action* action = actionLogger.getNewAction(ACTION_ARRANGEMENT_RECORD, false);

					arrangerView.xScrollWhenPlaybackStarted = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
					if (action) {
						action->posToClearArrangementFrom = arrangerView.xScrollWhenPlaybackStarted;
					}

					currentSong->clearArrangementBeyondPos(
					    arrangerView.xScrollWhenPlaybackStarted,
					    action); // Want to do this before setting up playback or place new instances
					int32_t error =
					    currentSong->placeFirstInstancesOfActiveClips(arrangerView.xScrollWhenPlaybackStarted);

					if (error) {
						display->displayError(error);
						return ActionResult::DEALT_WITH;
					}
					playbackHandler.recording = RECORDING_ARRANGEMENT;
					playbackHandler.setupPlaybackUsingInternalClock();

					arrangement.playbackStartedAtPos =
					    arrangerView.xScrollWhenPlaybackStarted; // Have to do this after setting up playback

					indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
					indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
					sessionButtonUsed = true;
				}
			}
		}
		// Release without special mode
		else if (!on && currentUIMode == UI_MODE_NONE) {
			if (lastSessionButtonActiveState && !sessionButtonActive && !sessionButtonUsed && !sessionView.gridFirstPadActive()) {
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					// Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					currentSong->resumeClipsClonedForArrangementRecording();
					playbackHandler.recording = RECORDING_OFF;
					view.setModLedStates();
					playbackHandler.setLedStates();
				}
				else {
					sessionView.goToArrangementEditor();
				}

				sessionButtonUsed = false;
			}
		}
	}

	// Affect-entire button
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			currentSong->affectEntire = !currentSong->affectEntire;
			view.setActiveModControllableTimelineCounter(currentSong);
		}
	}

	// Record button - adds to what MatrixDriver does with it
	else if (b == RECORD) {
		if (on) {
			if (isNoUIModeActive()) {
				uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 500);
				view.blinkOn = true;
			}
			else {
				goto notDealtWith;
			}
		}
		else {
			if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
				exitUIMode(UI_MODE_VIEWING_RECORD_ARMING);
				PadLEDs::reassessGreyout(false);
				requestRendering(this, 0, 0xFFFFFFFF);
			}
			else {
				goto notDealtWith;
			}
		}
		return ActionResult::NOT_DEALT_WITH; // Make the MatrixDriver do its normal thing with it too
	}

	// Select encoder button
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&sessionView);
		}
	}

	else {
notDealtWith:
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

void PerformanceSessionView::beginEditingSectionRepeatsNum() {
	performActionOnSectionPadRelease = false;
	drawSectionRepeatNumber();
	uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
}

ActionResult PerformanceSessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	//editPadAction
	if (xDisplay < kDisplayWidth) {
		auto [kind, id] = songParamsForPerformance[xDisplay];

		Param::Kind lastSelectedParamKind = kind;
		int32_t lastSelectedParamID = id;

		if (on) {			
			//ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(paramID);

			ModelStackWithAutoParam* modelStackWithParam = nullptr;
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			if (modelStackWithThreeMainThings) {
				ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSetSummary();
						
				if (summary) {
					ParamSet* paramSet = (ParamSet*)summary->paramCollection;
					modelStackWithParam =
						modelStackWithThreeMainThings->addParam(paramSet, summary, lastSelectedParamID, &paramSet->params[lastSelectedParamID]);
				}
			}
					
			if (modelStackWithParam && modelStackWithParam->autoParam) {

				if (modelStackWithParam->getTimelineCounter()
					== view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

					int32_t oldValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
					previousKnobPosition[xDisplay] = modelStackWithParam->paramCollection->paramValueToKnobPos(oldValue, modelStackWithParam);
					currentKnobPosition[xDisplay] = calculateKnobPosForSinglePadPress(yDisplay);

					int32_t newValue = modelStackWithParam->paramCollection->knobPosToParamValue(currentKnobPosition[xDisplay], modelStackWithParam);
						
					modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
																			view.modLength);

					char buffer[5];
					int32_t valueForDisplay = calculateKnobPosForDisplay(currentKnobPosition[xDisplay] + kKnobPosOffset);
					//intToString(valueForDisplay, buffer);
					//display->displayPopup(buffer);
					renderDisplayOLED(lastSelectedParamKind, lastSelectedParamID, valueForDisplay);

					uiNeedsRendering(this); //re-render mute pads
				}
			} 
		}
		else if (previousKnobPosition[xDisplay] != kNoSelection) {
			//ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(paramID);

			ModelStackWithAutoParam* modelStackWithParam = nullptr;
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

			if (modelStackWithThreeMainThings) {
				ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSetSummary();
						
				if (summary) {
					ParamSet* paramSet = (ParamSet*)summary->paramCollection;
					modelStackWithParam =
						modelStackWithThreeMainThings->addParam(paramSet, summary, lastSelectedParamID, &paramSet->params[lastSelectedParamID]);
				}
			}
					
			if (modelStackWithParam && modelStackWithParam->autoParam) {

				if (modelStackWithParam->getTimelineCounter()
					== view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

					int32_t oldValue = modelStackWithParam->paramCollection->knobPosToParamValue(previousKnobPosition[xDisplay], modelStackWithParam);
						
					modelStackWithParam->autoParam->setValuePossiblyForRegion(oldValue, modelStackWithParam, view.modPos,
																			view.modLength);

					char buffer[5];
					int32_t valueForDisplay = calculateKnobPosForDisplay(previousKnobPosition[xDisplay] + kKnobPosOffset);
					//intToString(valueForDisplay, buffer);
					//display->displayPopup(buffer);
					renderDisplayOLED(lastSelectedParamKind, lastSelectedParamID, valueForDisplay);

					previousKnobPosition[xDisplay] = kNoSelection;
					currentKnobPosition[xDisplay] = kNoSelection;

					uiNeedsRendering(this); //re-render mute pads
				}
			} 
		}	
	}

	return ActionResult::DEALT_WITH;
}

//get's the modelstack for the parameters that are being edited
ModelStackWithAutoParam* PerformanceSessionView::getModelStackWithParam(int32_t paramID) {

	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	if (modelStackWithThreeMainThings) {
		ParamCollectionSummary* summary = modelStackWithThreeMainThings->paramManager->getUnpatchedParamSetSummary();
				
		if (summary) {
			ParamSet* paramSet = (ParamSet*)summary->paramCollection;
			modelStackWithParam =
				modelStackWithThreeMainThings->addParam(paramSet, summary, paramID, &paramSet->params[paramID]);
		}
	}

	return modelStackWithParam;
}

int32_t PerformanceSessionView::calculateKnobPosForSinglePadPress(int32_t yDisplay) {
	int32_t newKnobPos = 0;

	//if you press bottom pad, value is 0, for all other pads except for the top pad, value = row Y * 18
	if (yDisplay < 7) {
		newKnobPos = yDisplay * kParamValueIncrementForAutomationSinglePadPress;
	}
	//if you are pressing the top pad, set the value to max (128)
	else {
		newKnobPos = kMaxKnobPos;
	}

	//in the deluge knob positions are stored in the range of -64 to + 64, so need to adjust newKnobPos set above.
	newKnobPos = newKnobPos - kKnobPosOffset;

	return newKnobPos;
}

//for non-MIDI instruments, convert deluge internal knobPos range to same range as used by menu's.
int32_t PerformanceSessionView::calculateKnobPosForDisplay(int32_t knobPos) {
	int32_t offset = 0;

	//convert knobPos from 0 - 128 to 0 - 50
	return (((((knobPos << 20) / kMaxKnobPos) * kMaxMenuValue) >> 20) - offset);
}

void PerformanceSessionView::renderDisplayOLED(Param::Kind lastSelectedParamKind, int32_t lastSelectedParamID, int32_t knobPos) {
	deluge::hid::display::OLED::clearMainImage();

	//display parameter name
	char parameterName[30];
	if (lastSelectedParamKind == Param::Kind::UNPATCHED) {
		strncpy(parameterName, getUnpatchedParamDisplayName(lastSelectedParamID), 29);
		}
	else if (lastSelectedParamKind == Param::Kind::GLOBAL_EFFECTABLE) {
		strncpy(parameterName, getGlobalEffectableParamDisplayName(lastSelectedParamID), 29);
	}

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	deluge::hid::display::OLED::drawStringCentred(parameterName, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

	//display parameter value
	yPos = yPos + 24;

	char buffer[5];
	intToString(knobPos, buffer);
	deluge::hid::display::OLED::drawStringCentred(buffer, yPos, deluge::hid::display::OLED::oledMainImage[0],
			                                      OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

	deluge::hid::display::OLED::sendMainImage();
}

ActionResult PerformanceSessionView::timerCallback() {
	switch (currentUIMode) {

	case UI_MODE_HOLDING_SECTION_PAD:
		beginEditingSectionRepeatsNum();
		break;

	case UI_MODE_NONE:
		if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			enterUIMode(UI_MODE_VIEWING_RECORD_ARMING);
			PadLEDs::reassessGreyout(false);
		case UI_MODE_VIEWING_RECORD_ARMING:
			requestRendering(this, 0, 0xFFFFFFFF);
			view.blinkOn = !view.blinkOn;
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, kFastFlashTime);
		}
		break;
	}

	return ActionResult::DEALT_WITH;
}

void PerformanceSessionView::drawSectionRepeatNumber() {
	int32_t number = currentSong->sections[sectionPressed].numRepetitions;
	char const* outputText;
	if (display->haveOLED()) {
		char buffer[21];
		if (number == -1) {
			outputText = "Launch non-\nexclusively"; // Need line break cos line splitter doesn't deal with hyphens.
		}
		else {
			outputText = buffer;
			strcpy(buffer, "Repeats: ");
			if (number == 0) {
				strcpy(&buffer[9], "infinite");
			}
			else {
				intToString(number, &buffer[9]);
			}
		}

		if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
			display->popupText(outputText);
		}
		else {
			display->popupTextTemporary(outputText);
		}
	}
	else {
		char buffer[5];
		if (number == -1) {
			outputText = "SHAR";
		}
		else if (number == 0) {
			outputText = "INFI";
		}
		else {
			intToString(number, buffer);
			outputText = buffer;
		}
		display->setText(outputText, true, 255, true);
	}
}

void PerformanceSessionView::selectEncoderAction(int8_t offset) {
	return;
}

ActionResult PerformanceSessionView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult PerformanceSessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

bool PerformanceSessionView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	return true;
}

void PerformanceSessionView::setLedStates() {

	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	view.setLedStates();

#ifdef currentClipStatusButtonX
	view.switchOffCurrentClipPad();
#endif
}

extern char loopsRemainingText[];

void PerformanceSessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	if (playbackHandler.isEitherClockActive()) {
		// Session playback
		if (currentPlaybackMode == &session) {
			if (session.launchEventAtSwungTickCount) {
yesDoIt:
				intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
				deluge::hid::display::OLED::drawPermanentPopupLookingText(loopsRemainingText);
			}
		}

		else { // Arrangement playback
			if (playbackHandler.stopOutputRecordingAtLoopEnd) {
				deluge::hid::display::OLED::drawPermanentPopupLookingText("Resampling will end...");
			}
		}
	}
}

void PerformanceSessionView::redrawNumericDisplay() {

	if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	// If playback on...
	if (playbackHandler.isEitherClockActive()) {

		// Session playback
		if (currentPlaybackMode == &session) {
			if (!session.launchEventAtSwungTickCount) {
				goto nothingToDisplay;
			}

			if (getCurrentUI() == &loadSongUI) {
				if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
yesDoIt:
					char buffer[5];
					intToString(session.numRepeatsTilLaunch, buffer);
					display->setText(buffer, true, 255, true, NULL, false, true);
				}
			}

			else if (getCurrentUI() == &arrangerView) {
				if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW
				    || currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
					if (session.switchToArrangementAtLaunchEvent) {
						goto yesDoIt;
					}
					else {
						goto setBlank;
					}
				}
			}

			else if (getCurrentUI() == this) {
				if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
					goto yesDoIt;
				}
			}
		}

		else { // Arrangement playback
			if (getCurrentUI() == &arrangerView) {

				if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) {
					if (playbackHandler.stopOutputRecordingAtLoopEnd) {
						display->setText("1", true, 255, true, NULL, false, true);
					}
					else {
						goto setBlank;
					}
				}
			}
			else if (getCurrentUI() == this) {
setBlank:
				display->setText("");
			}
		}
	}

	// Or if no playback active...
	else {
nothingToDisplay:
		if (getCurrentUI() == this || getCurrentUI() == &arrangerView) {
			if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
				display->setText("");
			}
		}
	}

	setCentralLEDStates();
}

// This gets called by redrawNumericDisplay() - or, if OLED, it gets called instead, because this still needs to happen.
void PerformanceSessionView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	if (getCurrentUI() == this) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

uint32_t PerformanceSessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

uint32_t PerformanceSessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

void PerformanceSessionView::graphicsRoutine() {
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
		if (modKnobMode == 4 && editingComp) { //upper
			counter = (counter + 1) % 5;
			if (counter == 0) {
				uint8_t gr = AudioEngine::mastercompressor.gainReduction;

				indicator_leds::setMeterLevel(1, gr); //Gain Reduction LED
			}
		}
	}
}

void PerformanceSessionView::requestRendering(UI* ui, uint32_t whichMainRows, uint32_t whichSideRows) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		// Just redrawing should be faster than evaluating every cell in every row
		uiNeedsRendering(ui, 0xFFFFFFFF, 0xFFFFFFFF);
	}

	uiNeedsRendering(ui, whichMainRows, whichSideRows);
}

void PerformanceSessionView::rowNeedsRenderingDependingOnSubMode(int32_t yDisplay) {

	switch (currentUIMode) {
	case UI_MODE_HORIZONTAL_SCROLL:
	case UI_MODE_HORIZONTAL_ZOOM:
	case UI_MODE_AUDIO_CLIP_EXPANDING:
	case UI_MODE_AUDIO_CLIP_COLLAPSING:
	case UI_MODE_INSTRUMENT_CLIP_EXPANDING:
	case UI_MODE_INSTRUMENT_CLIP_COLLAPSING:
	case UI_MODE_ANIMATION_FADE:
	case UI_MODE_EXPLODE_ANIMATION:
		break;

	default:
		requestRendering(this, 1 << yDisplay, 0);
	}
}

void PerformanceSessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	UI::modEncoderButtonAction(whichModEncoder, on);
	performActionOnPadRelease = false;
}

void PerformanceSessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
	performActionOnPadRelease = false;
}

bool PerformanceSessionView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
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
void PerformanceSessionView::performActualRender(uint32_t whichRows, uint8_t* image,
                                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                       int32_t xScroll, uint32_t xZoom, int32_t renderWidth,
                                                       int32_t imageWidth, bool drawUndefinedArea) {

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];

		renderRow(image + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
	}
}

//this function started off as a copy of the renderRow function from the NoteRow class - I replaced "notes" with "nodes"
//it worked for the most part, but there was bugs so I removed the buggy code and inserted my alternative rendering method
//which always works. hoping to bring back the other code once I've worked out the bugs.
void PerformanceSessionView::renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		uint8_t* pixel = image + (xDisplay * 3);

		memcpy(pixel, &rowTailColour[yDisplay], 3);
		occupancyMask[xDisplay] = 64;

		if ((yDisplay == (kDisplayHeight - 1)) && ((currentKnobPosition[xDisplay] + kKnobPosOffset) == kMaxKnobPos)) {
			memcpy(pixel, &rowBlurColour[yDisplay], 3);
		}
		else if (((currentKnobPosition[xDisplay] + kKnobPosOffset) / kParamValueIncrementForAutomationDisplay) == yDisplay) {
			memcpy(pixel, &rowBlurColour[yDisplay], 3);
		}
		occupancyMask[xDisplay] = 64;
	}
}

void PerformanceSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	performActionOnPadRelease = false;

	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}
