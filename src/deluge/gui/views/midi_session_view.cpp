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

#include "gui/views/midi_session_view.h"
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

const char* MIDI_DEFAULTS_XML = "MidiView.XML";
const char* MIDI_DEFAULTS_TAG = "defaults";
const char* MIDI_DEFAULTS_CC_TAG = "defaultCCMappings";

MidiSessionView midiSessionView{};

//initialize variables
MidiSessionView::MidiSessionView() {
	successfullyReadDefaultsFromFile = false;

	anyChangesToSave = false;
}

bool MidiSessionView::opened() {
	focusRegained();

	return true;
}

void MidiSessionView::focusRegained() {
	currentSong->affectEntire = true;

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	//	if (!successfullyReadDefaultsFromFile) {
	//		readDefaultsFromFile();
	//	}

	setLedStates();

	//	updateLayoutChangeStatus();

	if (display->have7SEG()) {
		redrawNumericDisplay();
	}

	uiNeedsRendering(this);
}

void MidiSessionView::graphicsRoutine() {
	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	// Nothing to do here but clear since we don't render playhead
	memset(&tickSquares, 255, sizeof(tickSquares));
	memset(&colours, 255, sizeof(colours));
	PadLEDs::setTickSquares(tickSquares, colours);
}

ActionResult MidiSessionView::timerCallback() {
	return ActionResult::DEALT_WITH;
}

bool MidiSessionView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
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

	//render performance view
	//	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
	//
	//		uint8_t* occupancyMaskOfRow = occupancyMask[yDisplay];
	//		int32_t imageWidth = kDisplayWidth + kSideBarWidth;
	//
	//		renderRow(&image[0][0][0] + (yDisplay * imageWidth * 3), occupancyMaskOfRow, yDisplay);
	//	}

	PadLEDs::renderingLock = false;

	return true;
}

/// render every column, one row at a time
void MidiSessionView::renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay) {
	return;
}

/// check if a midi cc has been assigned to any of the params
bool MidiSessionView::isMidiCCAssignedToParam() {
	return false;
}

/// nothing to render in sidebar (yet)
bool MidiSessionView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	return true;
}

/// render performance view display on opening
void MidiSessionView::renderViewDisplay() {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

		yPos = yPos + 12;

		//Render "Performance View" in the middle of the OLED screen
		deluge::hid::display::OLED::drawStringCentred(l10n::get(l10n::String::STRING_FOR_MIDI_VIEW), yPos,
		                                              deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();
	}
	else {
		display->setScrollingText(l10n::get(l10n::String::STRING_FOR_MIDI_VIEW));
	}
}

void MidiSessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	renderViewDisplay();
}

void MidiSessionView::redrawNumericDisplay() {
	renderViewDisplay();
}

void MidiSessionView::setLedStates() {
	setCentralLEDStates();  //inherited from session view
	view.setLedStates();    //inherited from session view
	view.setModLedStates(); //inherited from session view

	//performanceView specific LED settings
	indicator_leds::setLedState(IndicatorLED::MIDI, true);

	if (currentSong->lastClipInstanceEnteredStartPos != -1) {
		indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW);
	}
}

void MidiSessionView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);
}

ActionResult MidiSessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	// Clip-view button
	if (b == CLIP_VIEW) {
		if (on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))
		    && playbackHandler.recording != RECORDING_ARRANGEMENT) {
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
		else if (!on && ((currentUIMode == UI_MODE_NONE) || isUIModeActive(UI_MODE_STUTTERING))) {
			if (lastSessionButtonActiveState && !sessionButtonActive && !sessionButtonUsed
			    && !sessionView.gridFirstPadActive()) {

				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					// Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					currentSong->resumeClipsClonedForArrangementRecording();
					playbackHandler.recording = RECORDING_OFF;
					view.setModLedStates();
					playbackHandler.setLedStates();
				}

				sessionButtonUsed = false;
			}
		}
	}

	//clear and reset held params
	else if (b == BACK && isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (on) {
			return ActionResult::DEALT_WITH;
			//backupPerformanceLayout();
			//resetPerformanceView(modelStack);
			//logPerformanceLayoutChange();
		}
	}

	//save midi mappings
	else if (b == SAVE) {
		if (on) {
			return ActionResult::DEALT_WITH;
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_MIDI_DEFAULTS_SAVED));
		}
	}

	//load midi mappings
	else if (b == LOAD) {
		if (on) {
			return ActionResult::DEALT_WITH;
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_MIDI_DEFAULTS_LOADED));
		}
	}

	//enter "Perform FX" soundEditor menu
	else if ((b == SELECT_ENC) && !Buttons::isShiftButtonPressed()) {
		if (on) {
			return ActionResult::DEALT_WITH;
		}
	}

	//enter exit Horizontal Encoder Button Press UI Mode
	//(not used yet, will be though!)
	else if (b == X_ENC) {
		if (on) {
			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
		else {
			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}
	}

	//enter/exit Midi View
	else if (b == MIDI) {
		if (on) {
			if (currentSong->lastClipInstanceEnteredStartPos != -1) {
				changeRootUI(&arrangerView);
			}
			else {
				changeRootUI(&sessionView);
			}
		}
	}

	//disable button presses for Vertical encoder
	else if (b == Y_ENC) {
		return ActionResult::DEALT_WITH;
	}

	else {
		ActionResult buttonActionResult;
		buttonActionResult = TimelineView::buttonAction(b, on, inCardRoutine);

		//re-render grid if undoing an action (e.g. you previously loaded cc mappings)
		if (on && (b == BACK)) {
			uiNeedsRendering(this);
		}
		return buttonActionResult;
	}
	return ActionResult::DEALT_WITH;
}

ActionResult MidiSessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	//if pad was pressed in main deluge grid (not sidebar)
	if (xDisplay < kDisplayWidth) {
		uiNeedsRendering(this); //re-render pads
	}
	return ActionResult::DEALT_WITH;
}

/// check if pad press corresponds to a shortcut pad on the grid
bool MidiSessionView::isPadShortcut(int32_t xDisplay, int32_t yDisplay) {
	return false;
}

/// Used to edit a pad's value in editing mode
void MidiSessionView::selectEncoderAction(int8_t offset) {
	return;
}

ActionResult MidiSessionView::horizontalEncoderAction(int32_t offset) {
	return ActionResult::DEALT_WITH;
}

ActionResult MidiSessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

/// why do I need this? (code won't compile without it)
uint32_t MidiSessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

/// why do I need this? (code won't compile without it)
uint32_t MidiSessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

void MidiSessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	return;
}

/// used to reset stutter if it's already active
void MidiSessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	return;
}

void MidiSessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
}
