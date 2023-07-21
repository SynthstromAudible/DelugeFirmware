/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/arranger_view.h"
#include "processing/engines/audio_engine.h"
#include "model/clip/instrument_clip.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/browser/sample_browser.h"
#include "processing/sound/sound_instrument.h"
#include "RZA1/system/r_typedefs.h"
#include <string.h>
#include "gui/views/view.h"
#include "gui/ui/sound_editor.h"
#include "io/midi/midi_engine.h"
#include "gui/ui/audio_recorder.h"
#include "model/action/action_logger.h"
#include "playback/mode/playback_mode.h"
#include "model/song/song.h"
#include "hid/matrix/matrix_driver.h"
#include "model/note/note_row.h"
#include "hid/led/pad_leds.h"
#include "hid/led/indicator_leds.h"
#include "hid/buttons.h"
#include "extern.h"
#include "model/clip/clip_minder.h"
#include "gui/views/session_view.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"

#include "gui/ui/keyboard/layout/isomorphic.h"

keyboard::KeyboardScreen keyboardScreen{};

namespace keyboard {

layout::KeyboardLayoutIsomorphic keyboardLayoutIsomorphic{};
KeyboardLayout* layoutList[] = {(KeyboardLayout*)&keyboardLayoutIsomorphic, nullptr};

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

KeyboardScreen::KeyboardScreen() {
	memset(&pressedPads, 0, sizeof(pressedPads));
}

static const uint32_t padActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                            0}; // Careful - this is referenced in two places // I'm always careful ;)

int KeyboardScreen::padAction(int x, int y, int velocity) {
	if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow some of the time when in card routine.
	}

	// Handle overruling shortcut presses
	int soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, velocity);
	if (soundEditorResult != ACTION_RESULT_NOT_DEALT_WITH) {
		return soundEditorResult;
	}

	// Exit if pad is enabled but UI in wrong mode
	if (!isUIModeWithinRange(padActionUIModes)
	    && velocity) { //@TODO: Need to check if this can prevent changing root note
		return ACTION_RESULT_DEALT_WITH;
	}

	// Pad pressed down, add to list if not full
	if (velocity) {
		int freeSlotIdx = -1;
		for (int idx = 0; idx < MAX_NUM_KEYBOARD_PAD_PRESSES; ++idx) {
			// Free slot found
			if (!pressedPads[idx].active) {
				freeSlotIdx = idx;
				continue;
			}

			// Pad was already active
			if (pressedPads[idx].active && pressedPads[idx].x == x && pressedPads[idx].y == y) {
				freeSlotIdx = -1; // If a free slot was found previously, reset it so we don't write a second entry
				break;
			}
		}

		// Store active press in the free slot
		if (freeSlotIdx != -1) {
			pressedPads[freeSlotIdx].x = x;
			pressedPads[freeSlotIdx].y = y;
			pressedPads[freeSlotIdx].active = true;
		}
	}

	// Pad released, remove from list
	else {
		for (int idx = 0; idx < MAX_NUM_KEYBOARD_PAD_PRESSES; ++idx) {
			// Pad was already active
			if (pressedPads[idx].active && pressedPads[idx].x == x && pressedPads[idx].y == y) {
				pressedPads[idx].active = false;
				break;
			}
		}
	}

	layoutList[0]->evaluatePads(pressedPads);

	// // Handle setting root note //@TODO: only execute with exactly one new note
	if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
		// 	if (sdRoutineLock) {
		// 		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		// 	}

		// 	// We probably couldn't have got this far if it was a Kit, but let's just check
		// 	if (velocity && currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) {
		// 		//int noteCode = getNoteCodeFromCoords(x, y); // @TODO: Rewrite to use new note in activeNotes, needs to come after handlePad
		// 		exitScaleModeOnButtonRelease = false;
		// 		if (getCurrentClip()->inScaleMode) {
		// 			instrumentClipView.setupChangingOfRootNote(newNote);
		// 			requestRendering();
		// 			displayCurrentScaleName();
		// 		}
		// 		else {
		// 			enterScaleMode(newNote);
		// 		}
		// 	}
	}
	else {
		updateActiveNotes();
	}

	requestRendering();
	return ACTION_RESULT_DEALT_WITH;
}

NoteList lastActiveNotes; //@TODO: Move into class

void KeyboardScreen::updateActiveNotes() {
	/*
	NoteList activeNotes = layoutList[0]->getActiveNotes();

	//@TODO: Handle note list changes

	// Note added
	{
		int newNote = 0; //@TODO:

		// Flash Song button if another clip with the same instrument is currently playing
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);
		if (!clipIsActiveOnInstrument && newNote) { //@TODO: Fix
			indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
		}

		//@TODO: Only do this for physical and single individual presses
		// If note range menu is open and row is
		if (instrument->type == INSTRUMENT_TYPE_SYNTH) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(newNote + ((SoundInstrument*)instrument)->transpose);
			}
		}

		// Ensure the note the user is trying to sound isn't already sounding
		NoteRow* noteRow = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(newNote);
		if (noteRow) {
			if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
				return; //@TODO: Rewrite so just the following in Note added scope does not happen
			}
		}

		// Actually sounding the note
		if (instrument->type == INSTRUMENT_TYPE_KIT) {
			int velocityToSound = ((x % 4) * 8) + ((y % 4) * 32) + 7; //@TODO: Get velocity from note
			instrumentClipView.auditionPadAction(velocityToSound, yDisplay, false); //@TODO: Figure out how to factor out yDisplay
		}
		else {
			int velocityToSound = instrument->defaultVelocity;
			((MelodicInstrument*)instrument)
				->beginAuditioningForNote(modelStack, newNote, velocityToSound, zeroMPEValues);
		}

		//@TODO: Only do this for physical presses
		drawNoteCode(newNote);
		enterUIMode(UI_MODE_AUDITIONING);

		// Begin resampling - yup this is even allowed if we're in the card routine!
		if (Buttons::isButtonPressed(hid::button::RECORD) && !audioRecorder.recordingSource) {
			audioRecorder.beginOutputRecording();
			Buttons::recordButtonPressUsedUp = true;
		}
	}

	// Note removed
	{
		//@TODO: This code was run on pad up if the pad was not found in the pressed list, we need to replicate that
		// There were no presses. Just check we're not still stuck in "auditioning" mode, as users have still been reporting problems with this. (That comment from around 2021?)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			exitUIMode(UI_MODE_AUDITIONING);
		}


		if (instrument->type == INSTRUMENT_TYPE_KIT) { //
			instrumentClipView.auditionPadAction(0, yDisplay, false); //@TODO: Figure out how to factor out yDisplay
		}
		else {

			((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, noteCode);
		}

		//@TODO: If note list empty exit auditioning mode
		exitUIMode(UI_MODE_AUDITIONING);
	}

	//@TODO: Check whole list
	{
		// If anything at all still auditioning...
		int highestNoteCode = getHighestAuditionedNote();
		if (highestNoteCode != -2147483648) {
			drawNoteCode(highestNoteCode);
		}
		else {
#if HAVE_OLED
			OLED::removePopup();
#else
			redrawNumericDisplay();
#endif
		}
	}

	lastActiveNotes = activeNotes;


	// Recording - this only works *if* the Clip that we're viewing right now is the Instrument's activeClip
	//@TODO: Check if we can also enable this for kit instruments
	if (instrument->type != INSTRUMENT_TYPE_KIT && clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(currentSong->currentClip)) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(currentSong->currentClip);

		// Note-on
		if (velocity) {
			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) { // It definitely will be auditioning if we're here
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(0, NULL);
				((MelodicInstrument*)instrument)->earlyNotes.insertElementIfNonePresent(newNote, instrument->defaultVelocity, getCurrentClip()->allowNoteTails(modelStackWithNoteRow));
			}

			else {
				Action* action = actionLogger.getNewAction(ACTION_RECORD, true);

				bool scaleAltered = false;

				ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentClip()->getOrCreateNoteRowForYNote(newNote, modelStackWithTimelineCounter, action, &scaleAltered);
				NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
				if (thisNoteRow) {
					getCurrentClip()->recordNoteOn(modelStackWithNoteRow, instrument->defaultVelocity); //@TODO: Replace velocity

					// If this caused the scale to change, update scroll
					if (action && scaleAltered) {
						action->updateYScrollClipViewAfter();
					}
				}
			}
		}

		// Note-off
		else {
			ModelStackWithNoteRow* modelStackWithNoteRow =
				getCurrentClip()->getNoteRowForYNote(newNote, modelStackWithTimelineCounter);
			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				getCurrentClip()->recordNoteOff(modelStackWithNoteRow);
			}
		}
	}

	*/
}

int KeyboardScreen::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (inCardRoutine) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// Scale mode button
	if (b == SCALE_MODE) {
		if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			return ACTION_RESULT_DEALT_WITH; // Kits can't do scales!
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on) {
			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {

				// If user holding shift and we're already in scale mode, cycle through available scales
				if (Buttons::isShiftButtonPressed() && getCurrentClip()->inScaleMode) {
					cycleThroughScales();
					requestRendering();
				}

				// Or, no shift button - normal behaviour
				else {
					currentUIMode = UI_MODE_SCALE_MODE_BUTTON_PRESSED;
					exitScaleModeOnButtonRelease = true;
					if (!getCurrentClip()->inScaleMode) {
						calculateDefaultRootNote(); // Calculate it now so we can show the user even before they've released the button
						flashDefaultRootNoteOn = false;
						flashDefaultRootNote();
					}
				}
			}

			// // If user is auditioning just one note, we can go directly into Scale Mode and set that root note
			// else if (currentUIMode == UI_MODE_AUDITIONING && getActiveNoteCount() == 1 && !getCurrentClip()->inScaleMode) {
			// 	exitAuditionMode();
			// 	enterScaleMode(activeNotes[0]); //@TODO: Replace with working solution
			// }
		}
		else {
			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				currentUIMode = UI_MODE_NONE;
				if (getCurrentClip()->inScaleMode) {
					if (exitScaleModeOnButtonRelease) {
						exitScaleMode();
					}
				}
				else {
					enterScaleMode();
				}
			}
		}
	}

	// Keyboard button - exit mode
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeRootUI(&instrumentClipView);
		}
	}

	// Song view button
	else if (b == SESSION_VIEW && on && currentUIMode == UI_MODE_NONE) {
		// Transition back to arranger
		if (currentSong->lastClipInstanceEnteredStartPos != -1 || currentSong->currentClip->section == 255) {
			if (arrangerView.transitionToArrangementEditor()) {
				return ACTION_RESULT_DEALT_WITH;
			}
		}

		// Transition back to clip
		currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;
		int transitioningToRow = sessionView.getClipPlaceOnScreen(currentSong->currentClip);
		memcpy(&PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));
		memcpy(&PadLEDs::occupancyMaskStore, PadLEDs::occupancyMask, sizeof(PadLEDs::occupancyMask));
		//memset(PadLEDs::occupancyMaskStore, 16, sizeof(uint8_t) * displayHeight * (displayWidth + sideBarWidth));
		PadLEDs::numAnimatedRows = displayHeight;
		for (int y = 0; y < displayHeight; y++) {
			PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
			PadLEDs::animatedRowGoingFrom[y] = y;
		}

		PadLEDs::setupInstrumentClipCollapseAnimation(true);
		PadLEDs::recordTransitionBegin(clipCollapseSpeed);
		PadLEDs::renderClipExpandOrCollapse();
	}

	// Kit button
	else if (b == KIT
	         && currentUIMode == UI_MODE_NONE) { //@TODO: Conditional check depending if current layout supports kits
		if (on) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::KEYBOARD);
		}
	}

	else {
		requestRendering();
		int result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ACTION_RESULT_NOT_DEALT_WITH) {
			return result;
		}

		return view.buttonAction(
		    b, on,
		    inCardRoutine); // This might potentially do something while inCardRoutine but the condition above discards the call anyway
	}

	return ACTION_RESULT_DEALT_WITH;
}

int KeyboardScreen::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	if (Buttons::isShiftButtonPressed() && currentUIMode == UI_MODE_NONE) {
		getCurrentClip()->colourOffset += offset;
	}
	else {
		layoutList[0]->handleVerticalEncoder(offset);
		if (isUIModeWithinRange(padActionUIModes)) {
			layoutList[0]->evaluatePads(pressedPads);
			updateActiveNotes();
		}
	}

	requestRendering();
	return ACTION_RESULT_DEALT_WITH;
}

int KeyboardScreen::horizontalEncoderAction(int offset) {

	layoutList[0]->handleHorizontalEncoder(offset,
	                                       (Buttons::isShiftButtonPressed() && isUIModeWithinRange(padActionUIModes)));
	if (isUIModeWithinRange(padActionUIModes)) {
		layoutList[0]->evaluatePads(pressedPads);
		updateActiveNotes();
	}

	requestRendering();
	return ACTION_RESULT_DEALT_WITH;
}

void KeyboardScreen::selectEncoderAction(int8_t offset) {
	//@TODO: Add code to cycle through layouts if keyboard button is pressed and make sure it is not evaluated as exit
	//@TODO: Add code to cycle through scales if scale button is pressed and make sure it is not evaluated as scale mode set

	InstrumentClipMinder::selectEncoderAction(offset);
	instrumentClipView.recalculateColours();
	requestRendering();
}

void KeyboardScreen::exitAuditionMode() {
	layoutList[0]->stopAllNotes();
	updateActiveNotes();

	exitUIMode(UI_MODE_AUDITIONING);
#if !HAVE_OLED
	redrawNumericDisplay();
#endif
}

bool KeyboardScreen::opened() {
	focusRegained();
	openedInBackground();
	InstrumentClipMinder::opened();
	return true;
}

void KeyboardScreen::focusRegained() {
	InstrumentClipMinder::focusRegained();
	setLedStates();
}

void KeyboardScreen::openedInBackground() {
	getCurrentClip()->onKeyboardScreen = true;
	requestRendering(); // This one originally also included sidebar, the other ones didn't
}

bool KeyboardScreen::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                    uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	memset(image, 0, sizeof(uint8_t) * displayHeight * (displayWidth + sideBarWidth) * 3);
	memset(occupancyMask, 64,
	       sizeof(uint8_t) * displayHeight * (displayWidth + sideBarWidth)); // We assume the whole screen is occupied

	layoutList[0]->renderPads(image);

	return true;
}

bool KeyboardScreen::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                   uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (!image) {
		return true;
	}

	layoutList[0]->renderSidebarPads(image);

	return true;
}

void KeyboardScreen::flashDefaultRootNote() {
	uiTimerManager.setTimer(TIMER_DEFAULT_ROOT_NOTE, flashTime);
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	requestRendering();
}

void KeyboardScreen::enterScaleMode(int selectedRootNote) {

	int newScroll = instrumentClipView.setupForEnteringScaleMode(selectedRootNote);
	getCurrentClip()->yScroll = newScroll;

	displayCurrentScaleName();

	layoutList[0]->evaluatePads(pressedPads);
	updateActiveNotes();

	requestRendering();
	setLedStates();
}

void KeyboardScreen::exitScaleMode() {

	int scrollAdjust = instrumentClipView.setupForExitingScaleMode();
	getCurrentClip()->yScroll += scrollAdjust;

	layoutList[0]->evaluatePads(pressedPads);
	updateActiveNotes();

	requestRendering();
	setLedStates();
}

void KeyboardScreen::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
	InstrumentClipMinder::setLedStates();
}

void KeyboardScreen::drawNoteCode(int noteCode) {
	// Might not want to actually do this...
	if (!getCurrentUI()->toClipMinder()) {
		return;
	}

	if (currentSong->currentClip->output->type != INSTRUMENT_TYPE_KIT) {
		drawActualNoteCode(noteCode);
	}
}

bool KeyboardScreen::getAffectEntire() {
	return getCurrentClip()->affectEntire;
}

uint8_t keyboardTickSquares[displayHeight] = {255, 255, 255, 255, 255, 255, 255, 255};
const uint8_t keyboardTickColoursBasicRecording[displayHeight] = {0, 0, 0, 0, 0, 0, 0, 0};
const uint8_t keyboardTickColoursLinearRecording[displayHeight] = {0, 0, 0, 0, 0, 0, 0, 2};

void KeyboardScreen::graphicsRoutine() {
	int newTickSquare;

	const uint8_t* colours = keyboardTickColoursBasicRecording;

	if (!playbackHandler.isEitherClockActive() || !playbackHandler.isCurrentlyRecording()
	    || !currentSong->isClipActive(currentSong->currentClip) || currentUIMode == UI_MODE_EXPLODE_ANIMATION
	    || playbackHandler.ticksLeftInCountIn) {
		newTickSquare = 255;
	}
	else {
		newTickSquare = (uint64_t)(currentSong->currentClip->lastProcessedPos
		                           + playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick())
		                * displayWidth / currentSong->currentClip->loopLength;
		if (newTickSquare < 0 || newTickSquare >= displayWidth) {
			newTickSquare = 255;
		}

		if (currentSong->currentClip->getCurrentlyRecordingLinearly()) {
			colours = keyboardTickColoursLinearRecording;
		}
	}

	keyboardTickSquares[displayHeight - 1] = newTickSquare;

	PadLEDs::setTickSquares(keyboardTickSquares, colours);
}

} // namespace keyboard
