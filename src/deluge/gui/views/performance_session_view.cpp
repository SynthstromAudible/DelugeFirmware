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

PerformanceSessionView performanceSessionView{};

PerformanceSessionView::PerformanceSessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
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
	return ActionResult::DEALT_WITH;
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

	return true;
}

// Returns false if can't because in card routine
bool PerformanceSessionView::renderRow(ModelStack* modelStack, uint8_t yDisplay,
                            uint8_t thisImage[kDisplayWidth + kSideBarWidth][3],
                            uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	return true;
}

void PerformanceSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	performActionOnPadRelease = false;

	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}
