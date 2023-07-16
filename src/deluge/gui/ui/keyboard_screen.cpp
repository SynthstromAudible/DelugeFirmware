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

#include "gui/ui/keyboard_screen.h"
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

KeyboardScreen keyboardScreen{};

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

KeyboardScreen::KeyboardScreen() {
	memset(padPresses, 255, sizeof(padPresses));
	memset(yDisplayActive, 0, sizeof(yDisplayActive));
}

static const uint32_t padActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                            0}; // Careful - this is referenced in two places

int KeyboardScreen::padAction(int x, int y, int velocity) {

	if (x >= displayWidth) {
		return ACTION_RESULT_DEALT_WITH;
	}

	if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow some of the time when in card routine.
	}

	int soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, velocity);

	if (soundEditorResult != ACTION_RESULT_NOT_DEALT_WITH) {
		return soundEditorResult;
	}

	if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
		if (sdRoutineLock) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (velocity
		    && currentSong->currentClip->output->type
		           != INSTRUMENT_TYPE_KIT) { // We probably couldn't have got this far if it was a Kit, but let's just check
			int noteCode = getNoteCodeFromCoords(x, y);
			exitScaleModeOnButtonRelease = false;
			if (getCurrentClip()->inScaleMode) {
				instrumentClipView.setupChangingOfRootNote(noteCode);
				uiNeedsRendering(this, 0xFFFFFFFF, 0);
				displayCurrentScaleName();
			}
			else {
				enterScaleMode(noteCode);
			}
		}
	}
	else if (!velocity || isUIModeWithinRange(padActionUIModes)) {

		int noteCode;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);
		if (!clipIsActiveOnInstrument && velocity) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
		}

		Instrument* instrument = (Instrument*)currentSong->currentClip->output;

		// NOTE: Most of this refers to the Instrument's activeClip - *not* the Clip we're viewing,
		// which might not be the activeClip, even though we did call makeClipActiveOnInstrumentIfPossible() above

		// Press-down
		if (velocity) {

			int emptyPressIndex = MAX_NUM_KEYBOARD_PAD_PRESSES;

			// Look for an empty pad press - or an existing press of the same physical pad - which could be left
			// if we missed a press-off event somehow (think this was happening when switching presets sometimes)
			for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
				if ((padPresses[p].x == x && padPresses[p].y == y)
				    || (emptyPressIndex == MAX_NUM_KEYBOARD_PAD_PRESSES && padPresses[p].x == 255)) {
					emptyPressIndex = p;
				}
			}

			// If no spare presses, return
			if (emptyPressIndex == MAX_NUM_KEYBOARD_PAD_PRESSES) {
				return ACTION_RESULT_DEALT_WITH;
			}

			noteCode = getNoteCodeFromCoords(x, y);

			int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
			if (instrument->type == INSTRUMENT_TYPE_KIT) { //
				// yDisplay = (int)(x / 4) + (int)(y / 4) * 4;
				yDisplay = y;
			}
			if (yDisplayActive[yDisplay]) {
				return ACTION_RESULT_DEALT_WITH;
			}

			// Change editing range if necessary
			if (instrument->type == INSTRUMENT_TYPE_SYNTH) {
				if (velocity) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(noteCode
						                                              + ((SoundInstrument*)instrument)->transpose);
					}
				}
			}

			// Ensure the note the user is trying to sound isn't already sounding
			NoteRow* noteRow = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(noteCode);
			if (noteRow) {
				if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
					return ACTION_RESULT_DEALT_WITH;
				}
			}

			// Only now that we know we're not going to return prematurely can we mark the pad as pressed
			{
				padPresses[emptyPressIndex].x = x;
				padPresses[emptyPressIndex].y = y;
				yDisplayActive[yDisplay] = true;
			}

			{
				if (instrument->type == INSTRUMENT_TYPE_KIT) {
					// int velocityToSound = ((x % 4) * 8) + ((y % 4) * 32) + 7;
					int velocityToSound = x * 8 + 7;
					instrumentClipView.auditionPadAction(velocityToSound, yDisplay, false);
				}
				else {
					int velocityToSound = instrument->defaultVelocity;
					((MelodicInstrument*)instrument)
					    ->beginAuditioningForNote(modelStack, noteCode, velocityToSound, zeroMPEValues);
				}
			}

			drawNoteCode(noteCode);
			enterUIMode(UI_MODE_AUDITIONING);

			// Begin resampling - yup this is even allowed if we're in the card routine!
			if (Buttons::isButtonPressed(hid::button::RECORD) && !audioRecorder.recordingSource) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}
		}

		// Press-up
		else {

			int p;
			for (p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
				if (padPresses[p].x == x && padPresses[p].y == y) {
					goto foundIt;
				}
			}

			// There were no presses. Just check we're not still stuck in "auditioning" mode, as users have still been reporting problems with this. (That comment from around 2021?)
			if (isUIModeActive(UI_MODE_AUDITIONING)) {
				exitUIMode(UI_MODE_AUDITIONING);
			}
			return ACTION_RESULT_DEALT_WITH;

foundIt:
			padPresses[p].x = 255;
			noteCode = getNoteCodeFromCoords(x, y);
			int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
			if (instrument->type == INSTRUMENT_TYPE_KIT) { //
				// yDisplay = (int)(x / 4) + (int)(y / 4) * 4;
				yDisplay = y;
			}

			// We need to check that we had actually switched the note on here - it might have already been sounding, from the sequence
			if (!yDisplayActive[yDisplay]) {
				return ACTION_RESULT_DEALT_WITH;
			}

			// If any other of the same note is being held down, then don't switch it off. Also, see if we're still "auditioning" any notes at all
			exitUIMode(UI_MODE_AUDITIONING);

			// If any pad press still happening...
			for (p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
				if (padPresses[p].x != 255) {
					enterUIMode(UI_MODE_AUDITIONING);
					// ...then we're still auditioning

					// If the same note is still being held down (on a different pad), then we don't want to switch it off either
					if (getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y) == noteCode) {
						return ACTION_RESULT_DEALT_WITH;
					}
				}
			}

			// If we had indeed sounded the note via audition (as opposed to it being on in the sequence), switch it off.
			// If that was not the case, well, we still did want to potentially exit audition mode above, cos users been reporting stuck note problems,
			// even though I can't see quite how we'd get stuck there
			if (yDisplayActive[yDisplay]) {

				if (instrument->type == INSTRUMENT_TYPE_KIT) { //
					instrumentClipView.auditionPadAction(0, yDisplay, false);
				}
				else {

					((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, noteCode);
				}

				yDisplayActive[yDisplay] = false;
			}

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

		// Recording - this only works *if* the Clip that we're viewing right now is the Instrument's activeClip
		if (instrument->type != INSTRUMENT_TYPE_KIT && clipIsActiveOnInstrument
		    && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(currentSong->currentClip)) {

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(currentSong->currentClip);

			// Note-on
			if (velocity) {

				// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
				// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
				// in MelodicInstrument and Kit
				if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) { // It definitely will be auditioning if we're here
					ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(0, NULL);
					((MelodicInstrument*)instrument)
					    ->earlyNotes.insertElementIfNonePresent(
					        noteCode, instrument->defaultVelocity,
					        getCurrentClip()->allowNoteTails(modelStackWithNoteRow));
				}

				else {
					Action* action = actionLogger.getNewAction(ACTION_RECORD, true);

					bool scaleAltered = false;

					ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentClip()->getOrCreateNoteRowForYNote(
					    noteCode, modelStackWithTimelineCounter, action, &scaleAltered);
					NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
					if (thisNoteRow) {
						getCurrentClip()->recordNoteOn(modelStackWithNoteRow, instrument->defaultVelocity);

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
				    getCurrentClip()->getNoteRowForYNote(noteCode, modelStackWithTimelineCounter);
				if (modelStackWithNoteRow->getNoteRowAllowNull()) {
					getCurrentClip()->recordNoteOff(modelStackWithNoteRow);
				}
			}
		}

		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}

	return ACTION_RESULT_DEALT_WITH;
}

int KeyboardScreen::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// Scale mode button
	if (b == SCALE_MODE) {
		if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
			return ACTION_RESULT_DEALT_WITH; // Kits can't do scales!
		}

		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on) {
			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {

				// If user holding shift and we're already in scale mode, cycle through available scales
				if (Buttons::isShiftButtonPressed() && getCurrentClip()->inScaleMode) {
					cycleThroughScales();
					uiNeedsRendering(this, 0xFFFFFFFF, 0);
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

			// If user is auditioning just one note, we can go directly into Scale Mode and set that root note
			else if (oneNoteAuditioning() && !getCurrentClip()->inScaleMode) {
				exitAuditionMode();
				enterScaleMode(getLowestAuditionedNote());
			}
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
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeRootUI(&instrumentClipView);
		}
	}

	// Song view button
	else if (b == SESSION_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentSong->lastClipInstanceEnteredStartPos != -1 || currentSong->currentClip->section == 255) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}

			else {
doOther:
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
		}
	}

	// Kit button
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		if (on) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::KEYBOARD);
		}
	}

	else {
		uiNeedsRendering(this, 0xFFFFFFFF, 0); //
		int result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ACTION_RESULT_NOT_DEALT_WITH) {
			return result;
		}

		return view.buttonAction(b, on, inCardRoutine);
	}

	return ACTION_RESULT_DEALT_WITH;
}

void KeyboardScreen::selectEncoderAction(int8_t offset) {
	InstrumentClipMinder::selectEncoderAction(offset);
	instrumentClipView.recalculateColours();
	uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

int KeyboardScreen::getNoteCodeFromCoords(int x, int y) {

	Instrument* instrument = (Instrument*)currentSong->currentClip->output;
	if (instrument->type == INSTRUMENT_TYPE_KIT) { //
		// return 60 + (int)(x / 4) + (int)(y / 4) * 4;
		return 60 + y;
	}
	else {
		InstrumentClip* clip = getCurrentClip();
		return clip->yScrollKeyboardScreen + x + y * clip->keyboardRowInterval;
	}
}

void KeyboardScreen::exitAuditionMode() {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	stopAllAuditioning(modelStack);

	memset(yDisplayActive, 0, sizeof(yDisplayActive));
	exitUIMode(UI_MODE_AUDITIONING);
#if !HAVE_OLED
	redrawNumericDisplay();
#endif
}

void KeyboardScreen::stopAllAuditioning(ModelStack* modelStack, bool switchOffOnThisEndToo) {

	for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
		if (padPresses[p].x != 255) {
			int noteCode = getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y);
			((MelodicInstrument*)currentSong->currentClip->output)->endAuditioningForNote(modelStack, noteCode);
			if (switchOffOnThisEndToo) {
				padPresses[p].x = 255;
			}
		}
	}
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
	recalculateColours();
	uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
}

void KeyboardScreen::recalculateColours() {
	InstrumentClip* clip = getCurrentClip();
	for (int i = 0; i < displayHeight * clip->keyboardRowInterval + displayWidth; i++) {
		clip->getMainColourFromY(clip->yScrollKeyboardScreen + i, 0, noteColours[i]);
	}
}

bool KeyboardScreen::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                    uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	// First, piece together a picture of all notes-within-an-octave which are active
	bool notesWithinOctaveActive[12];
	memset(notesWithinOctaveActive, 0, sizeof(notesWithinOctaveActive));
	for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
		if (padPresses[p].x != 255) {
			int noteCode = getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y);
			int noteWithinOctave = (noteCode - currentSong->rootNote + 120) % 12;
			notesWithinOctaveActive[noteWithinOctave] = true;
		}
	}

	memset(image, 0, sizeof(uint8_t) * displayHeight * (displayWidth + sideBarWidth) * 3);
	memset(occupancyMask, 0, sizeof(uint8_t) * displayHeight * (displayWidth + sideBarWidth));

	uint8_t noteColour[] = {127, 127, 127};
	Instrument* instrument = (Instrument*)currentSong->currentClip->output;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
	    modelStack->addTimelineCounter(currentSong->currentClip);

	// Flashing default root note
	if (uiTimerManager.isTimerSet(TIMER_DEFAULT_ROOT_NOTE)) {
		if (flashDefaultRootNoteOn) {
			for (int y = 0; y < displayHeight; y++) {
				int noteCode = getNoteCodeFromCoords(0, y);
				int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
				int noteWithinOctave = (noteCode - defaultRootNote + 120) % 12;
				for (int x = 0; x < displayWidth; x++) {

					if (!noteWithinOctave) {
						memcpy(image[y][x], noteColours[yDisplay], 3);
					}

					yDisplay++;
					noteWithinOctave++;
					if (noteWithinOctave == 12) {
						noteWithinOctave = 0;
					}
				}
			}
		}
	}

	// Or normal
	else {
		for (int y = 0; y < displayHeight; y++) {
			int noteCode = getNoteCodeFromCoords(0, y);
			int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
			int noteWithinOctave = (uint16_t)(noteCode - currentSong->rootNote + 120) % (uint8_t)12;

			for (int x = 0; x < displayWidth; x++) {

				// If auditioning note with finger - or same note in different octave...
				if (notesWithinOctaveActive[noteWithinOctave]) {
doFullColour:
					memcpy(image[y][x], noteColours[yDisplay], 3);
					occupancyMask[y][x] = 64;
				}
				// Show root note within each octave as full colour
				else if (!noteWithinOctave) {
					goto doFullColour;

					// Or, if this note is just within the current scale, show it dim
				}
				else {
					if (getCurrentClip()->inScaleMode && currentSong->modeContainsYNote(noteCode)) {
						getTailColour(image[y][x], noteColours[yDisplay]);
						occupancyMask[y][x] = 1;
					}
				}

				if (instrument->type == INSTRUMENT_TYPE_KIT) {
					// int myV = ((x % 4) * 16) + ((y % 4) * 64) + 8;
					// int myY = (int)(x / 4) + (int)(y / 4) * 4;
					int myV = x * 16 + 8;
					int myY = y;

					ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
					    getCurrentClip()->getNoteRowOnScreen(myY, modelStackWithTimelineCounter);

					if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

						instrumentClipView.getRowColour(myY, noteColour);

						noteColour[0] = (char)((noteColour[0] * myV / 255) / 3);
						noteColour[1] = (char)((noteColour[1] * myV / 255) / 3);
						noteColour[2] = (char)((noteColour[2] * myV / 255) / 3);
					}
					else {
						noteColour[0] = 2;
						noteColour[1] = 2;
						noteColour[2] = 2;
					}
					memcpy(image[y][x], noteColour, 3);
				}
				// Otherwise, square will just get left black, from its having been wiped above

				// If we're selecting ranges...
				if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
				    || (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
					if (soundEditor.isUntransposedNoteWithinRange(noteCode)) {
						for (int colour = 0; colour < 3; colour++) {
							int value = (int)image[y][x][colour] + 35;
							image[y][x][colour] = getMin(value, 255);
						}
					}
				}

				noteCode++;
				yDisplay++;
				noteWithinOctave++;
				if (noteWithinOctave == 12) {
					noteWithinOctave = 0;
				}
			}
		}
	}
	return true;
}

bool KeyboardScreen::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                   uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (!image) {
		return true;
	}

	for (int y = 0; y < displayHeight; y++) {
		memset(image[y][displayWidth], 0, sideBarWidth * 3);
	}

	return true;
}

int KeyboardScreen::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed()) {
		if (currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
			}

			getCurrentClip()->colourOffset += offset;
			recalculateColours();
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}
	else {
		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
		}

		//
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == INSTRUMENT_TYPE_KIT) { //
			// instrumentClipView.verticalEncoderAction(offset * 4, inCardRoutine);
			instrumentClipView.verticalEncoderAction(offset, inCardRoutine);
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
		else {
			doScroll(offset * getCurrentClip()->keyboardRowInterval);
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

int KeyboardScreen::horizontalEncoderAction(int offset) {
	Instrument* instrument = (Instrument*)currentSong->currentClip->output;
	if (instrument->type != INSTRUMENT_TYPE_KIT) {
		if (Buttons::isShiftButtonPressed()) {
			if (isUIModeWithinRange(padActionUIModes)) {
				InstrumentClip* clip = getCurrentClip();
				clip->keyboardRowInterval += offset;
				if (clip->keyboardRowInterval < 1) {
					clip->keyboardRowInterval = 1;
				}
				else if (clip->keyboardRowInterval > KEYBOARD_ROW_INTERVAL_MAX) {
					clip->keyboardRowInterval = KEYBOARD_ROW_INTERVAL_MAX;
				}

				char buffer[13] = "row step:   ";
				intToString(clip->keyboardRowInterval, buffer + (HAVE_OLED ? 10 : 0), 1);
				numericDriver.displayPopup(buffer);

				doScroll(0, true);
			}
		}
		else {
			doScroll(offset);
		}
	}
	else if (instrument->type == INSTRUMENT_TYPE_KIT) {
		instrumentClipView.verticalEncoderAction(offset, false);
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}

	return ACTION_RESULT_DEALT_WITH;
}

void KeyboardScreen::doScroll(int offset, bool force) {

	if (isUIModeWithinRange(padActionUIModes)) {

		// Check we're not scrolling out of range
		int newYNote;
		if (offset >= 0) {
			newYNote = getCurrentClip()->yScrollKeyboardScreen
			           + (displayHeight - 1) * getCurrentClip()->keyboardRowInterval + displayWidth - 1;
		}
		else {
			newYNote = getCurrentClip()->yScrollKeyboardScreen;
		}
		if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
			return;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		stopAllAuditioning(modelStack, false);

		getCurrentClip()->yScrollKeyboardScreen += offset;

		recalculateColours();
		uiNeedsRendering(this, 0xFFFFFFFF, 0);

		int highestNoteCode = getHighestAuditionedNote();
		if (highestNoteCode != -2147483648) {
			drawNoteCode(highestNoteCode);

			// Change editing range if necessary
			if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_SYNTH) {
				if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
					menu_item::multiRangeMenu.noteOnToChangeRange(
					    highestNoteCode + ((SoundInstrument*)currentSong->currentClip->output)->transpose);
				}
			}
		}

		// All notes on
		for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
			if (padPresses[p].x != 255) {
				int noteCode = getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y);

				// Ensure the note the user is trying to sound isn't already sounding
				NoteRow* noteRow = getCurrentClip()->getNoteRowForYNote(noteCode);
				if (noteRow) {
					if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
						continue;
					}
				}

				((MelodicInstrument*)currentSong->currentClip->output)
				    ->beginAuditioningForNote(modelStack, noteCode,
				                              ((Instrument*)currentSong->currentClip->output)->defaultVelocity,
				                              zeroMPEValues);
			}
		}
	}
}

void KeyboardScreen::flashDefaultRootNote() {
	uiTimerManager.setTimer(TIMER_DEFAULT_ROOT_NOTE, flashTime);
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

bool KeyboardScreen::oneNoteAuditioning() {
	if (currentUIMode != UI_MODE_AUDITIONING) {
		return false;
	}

	int numFound = 0;

	for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
		if (padPresses[p].x == 255) {
			numFound++;
			if (numFound > 1) {
				return false;
			}
		}
	}
	return (numFound == 1);
}

int KeyboardScreen::getLowestAuditionedNote() {
	int lowestNote = 2147483647;

	for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
		if (padPresses[p].x != 255) {
			int noteCode = getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y);
			if (noteCode < lowestNote) {
				lowestNote = noteCode;
			}
		}
	}

	return lowestNote;
}

int KeyboardScreen::getHighestAuditionedNote() {
	int highestNote = -2147483648;

	for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
		if (padPresses[p].x != 255) {
			int noteCode = getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y);
			if (noteCode > highestNote) {
				highestNote = noteCode;
			}
		}
	}

	return highestNote;
}

void KeyboardScreen::enterScaleMode(int selectedRootNote) {

	int newScroll = instrumentClipView.setupForEnteringScaleMode(selectedRootNote);
	getCurrentClip()->yScroll = newScroll;

	displayCurrentScaleName();

	// And tidy up
	uiNeedsRendering(this, 0xFFFFFFFF, 0);
	setLedStates();
}

void KeyboardScreen::exitScaleMode() {

	int scrollAdjust = instrumentClipView.setupForExitingScaleMode();
	getCurrentClip()->yScroll += scrollAdjust;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
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
