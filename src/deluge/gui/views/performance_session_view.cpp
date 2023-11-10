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

//sorted in the order that Parameters are assigned to performance mode columns on the grid
const std::array<std::pair<Param::Kind, ParamType>, kDisplayWidth>
    songParamsForPerformance{{
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_FREQ}, //LPF Cutoff, Resonance
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::LPF_RES},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_FREQ}, //HPF Cutoff, Resonance
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::HPF_RES},
        {Param::Kind::UNPATCHED, Param::Unpatched::BASS}, //Bass
        {Param::Kind::UNPATCHED, Param::Unpatched::TREBLE}, //Treble
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT}, //Reverb Amount
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_AMOUNT},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::DELAY_RATE},         //Delay Rate, Amount
       	{Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_OFFSET}, //Mod FX Offset, Feedback, Depth, Rate
        {Param::Kind::UNPATCHED, Param::Unpatched::MOD_FX_FEEDBACK},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH},
        {Param::Kind::GLOBAL_EFFECTABLE, Param::Unpatched::GlobalEffectable::MOD_FX_RATE},
        {Param::Kind::UNPATCHED, Param::Unpatched::SAMPLE_RATE_REDUCTION}, //Decimation, Bitcrush
        {Param::Kind::UNPATCHED, Param::Unpatched::BITCRUSHING},
		{Param::Kind::UNPATCHED, Param::Unpatched::STUTTER_RATE},      //Stutter Rate
    }};

//colours for the performance mode

const uint8_t rowColour[kDisplayWidth][3] = {

    {255,0,0}, //LPF Cutoff
	{255,0,0}, //LPF Resonance
	{221,72,13}, //HPF Cutoff
	{221,72,13}, //HPF Resonance
	{170,182,0}, //EQ Bass
	{170,182,0}, //EQ Treble
	{85,182,72}, //Reverb Amount
	{51,109,145}, //Delay Amount
	{51,109,145}, //Delay Rate
	{144,72,91}, //Mod FX Offset
	{144,72,91}, //Mod FX Feedback
	{144,72,91}, //Mod FX Depth
	{144,72,91}, //Mod FX Rate
	{128,0,128}, //Decimation
	{128,0,128}, //Bitcrush
	{0,0,255}}; //Stutter

const uint8_t rowTailColour[kDisplayWidth][3] = {

    {53,2,2}, //LPF Cutoff
	{53,2,2}, //LPF Resonance
	{46,16,2}, //HPF Cutoff
	{46,16,2}, //HPF Resonance
	{36,38,2}, //EQ Bass
	{36,38,2}, //EQ Treble
	{19,38,16}, //Reverb Amount
	{12,23,31}, //Delay Amount
	{12,23,31}, //Delay Rate
	{37,15,37}, //Mod FX Offset
	{37,15,37}, //Mod FX Feedback
	{37,15,37}, //Mod FX Depth
	{37,15,37}, //Mod FX Rate
	{53,0,53}, //Decimation
	{53,0,53}, //Bitcrush
	{2,2,53}}; //Stutter

PerformanceSessionView performanceSessionView{};

PerformanceSessionView::PerformanceSessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;

	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		previousKnobPosition[xDisplay] = kNoSelection;
		currentKnobPosition[xDisplay] = kNoSelection;
		previousPadPressYDisplay[xDisplay] = kNoSelection;
		timeLastPadPress[xDisplay] = 0;
		padPressHeld[xDisplay] = false;
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

	return true;
}

void PerformanceSessionView::focusRegained() {
	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);

	currentSong->affectEntire = true;
	currentSong->performanceView = true;

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

	uiNeedsRendering(this);
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

ActionResult PerformanceSessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	//editPadAction
	if (xDisplay < kDisplayWidth) {
		auto [kind, id] = songParamsForPerformance[xDisplay];

		Param::Kind lastSelectedParamKind = kind;
		int32_t lastSelectedParamID = id;

		if (on) {	
			if (currentKnobPosition[xDisplay] == kNoSelection) {		
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

						previousPadPressYDisplay[xDisplay] = yDisplay;
						timeLastPadPress[xDisplay] = AudioEngine::audioSampleTimer;

						int32_t oldValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(view.modPos, modelStackWithParam);
						previousKnobPosition[xDisplay] = modelStackWithParam->paramCollection->paramValueToKnobPos(oldValue, modelStackWithParam);
						currentKnobPosition[xDisplay] = calculateKnobPosForSinglePadPress(yDisplay);

						int32_t newValue = modelStackWithParam->paramCollection->knobPosToParamValue(currentKnobPosition[xDisplay], modelStackWithParam);
							
						modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, view.modPos,
																				view.modLength);

						if ((lastSelectedParamKind == Param::Kind::UNPATCHED) && (lastSelectedParamID == Param::Unpatched::STUTTER_RATE)) {
							((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
													->beginStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
						}

						int32_t valueForDisplay = calculateKnobPosForDisplay(currentKnobPosition[xDisplay] + kKnobPosOffset);
						if (display->haveOLED()) {
							renderDisplayOLED(lastSelectedParamKind, lastSelectedParamID, valueForDisplay);
						}
						else {
							char buffer[5];
							intToString(valueForDisplay, buffer);
							display->displayPopup(buffer);
						}

						goto renderPads;
					}
				} 
			}
		}
		else {
			if ((previousKnobPosition[xDisplay] != kNoSelection) && (previousPadPressYDisplay[xDisplay] == yDisplay) && ((AudioEngine::audioSampleTimer - timeLastPadPress[xDisplay]) >= kShortPressTime)) {
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

						if ((lastSelectedParamKind == Param::Kind::UNPATCHED) && (lastSelectedParamID == Param::Unpatched::STUTTER_RATE)) {
							((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
													->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
						}					
						
						int32_t valueForDisplay = calculateKnobPosForDisplay(previousKnobPosition[xDisplay] + kKnobPosOffset);
						if (display->haveOLED()) {
							renderDisplayOLED(lastSelectedParamKind, lastSelectedParamID, valueForDisplay);
						}
						else {
							char buffer[5];
							intToString(valueForDisplay, buffer);
							display->displayPopup(buffer);
						}

						previousPadPressYDisplay[xDisplay] = kNoSelection;
						previousKnobPosition[xDisplay] = kNoSelection;
						currentKnobPosition[xDisplay] = kNoSelection;
						padPressHeld[xDisplay] = false;

						goto renderPads;
					}
				} 
			}
			else if (previousKnobPosition[xDisplay] != kNoSelection) {
				padPressHeld[xDisplay] = true;
				goto renderPads;
			}
		}	
		goto done;
	renderPads:
		uiNeedsRendering(this); //re-render pads
	}

done:
	return ActionResult::DEALT_WITH;
}

//get's the modelstack for the parameters that are being edited
//not working atm
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

//convert deluge internal knobPos range to same range as used by menu's.
int32_t PerformanceSessionView::calculateKnobPosForDisplay(int32_t knobPos) {
	int32_t offset = 0;

	//convert knobPos from 0 - 128 to 0 - 50
	return (((((knobPos << 20) / kMaxKnobPos) * kMaxMenuValue) >> 20) - offset);
}

//render performance view display on opening
void PerformanceSessionView::renderDisplay() {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();
			
	#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
	#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
	#endif	

		yPos = yPos + 12;

		deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_PERFORMANCE), yPos, deluge::hid::display::OLED::oledMainImage[0],
													OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();			
	}
	else {
		display->setScrollingText(l10n::get(l10n::String::STRING_FOR_PERFORMANCE));
	}
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
	return ActionResult::DEALT_WITH;
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
	view.setModLedStates();

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

void PerformanceSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	performActionOnPadRelease = false;

	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
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

		if ((currentKnobPosition[xDisplay] != kNoSelection) && (padPressHeld[xDisplay] == false)) {
			memcpy(pixel, &rowColour[xDisplay], 3);
		}
		else {
			memcpy(pixel, &rowTailColour[xDisplay], 3);
		}

		if (currentKnobPosition[xDisplay] != kNoSelection) {
			if ((yDisplay == (kDisplayHeight - 1)) && ((currentKnobPosition[xDisplay] + kKnobPosOffset) == kMaxKnobPos)) {
				goto highlightPad;
			}
			else if (((currentKnobPosition[xDisplay] + kKnobPosOffset) / kParamValueIncrementForAutomationDisplay) == yDisplay) {
				goto highlightPad;
			}
			goto finishRender;
		highlightPad:
			pixel[0] = 130;
			pixel[1] = 120;
			pixel[2] = 130;
		}
	finishRender:
		occupancyMask[xDisplay] = 64;
	}
}
