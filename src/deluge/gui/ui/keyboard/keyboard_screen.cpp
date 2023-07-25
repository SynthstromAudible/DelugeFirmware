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
#include "definitions_cxx.hpp"
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
#include "gui/ui/keyboard/layout/velocity_drums.h"
#include "gui/ui/keyboard/layout/in_key.h"

keyboard::KeyboardScreen keyboardScreen{};

namespace keyboard {

layout::KeyboardLayoutIsomorphic keyboardLayoutIsomorphic{};
layout::KeyboardLayoutVelocityDrums keyboardLayoutVelocityDrums{};
layout::KeyboardLayoutInKey keyboardLayoutInKey{};
KeyboardLayout* layoutList[KeyboardLayoutType::MaxElement + 1] = {0};

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

Instrument* getActiveInstrument() {
	return (Instrument*)currentSong->currentClip->output;
}

KeyboardScreen::KeyboardScreen() {
	layoutList[KeyboardLayoutType::Isomorphic] = (KeyboardLayout*)&keyboardLayoutIsomorphic;
	layoutList[KeyboardLayoutType::Drums] = (KeyboardLayout*)&keyboardLayoutVelocityDrums;
	layoutList[KeyboardLayoutType::InKey] = (KeyboardLayout*)&keyboardLayoutInKey;

	memset(&pressedPads, 0, sizeof(pressedPads));
	currentNotesState = {0};
	lastNotesState = {0};
}

static const uint32_t padActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                            0}; // Careful - this is referenced in two places // I'm always careful ;)

ActionResult KeyboardScreen::padAction(int x, int y, int velocity) {
	if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow some of the time when in card routine.
	}

	// Handle overruling shortcut presses
	ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, velocity);
	if (soundEditorResult != ActionResult::NOT_DEALT_WITH) {
		return soundEditorResult;
	}

	// Exit if pad is enabled but UI in wrong mode, this was removed as it prevented from changing root note
	// if (!isUIModeWithinRange(padActionUIModes)
	//     && velocity) {
	// 	return ActionResult::DEALT_WITH;
	// }

	// Pad pressed down, add to list if not full
	if (velocity) {
		int freeSlotIdx = -1;
		for (int idx = 0; idx < kMaxNumKeyboardPadPresses; ++idx) {
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
		for (int idx = 0; idx < kMaxNumKeyboardPadPresses; ++idx) {
			// Pad was already active
			if (pressedPads[idx].active && pressedPads[idx].x == x && pressedPads[idx].y == y) {
				pressedPads[idx].active = false;
				break;
			}
		}
	}

	evaluateActiveNotes();

	// Handle setting root note
	if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// We probably couldn't have got this far if it was a Kit, but let's just check
		if (getActiveInstrument()->type != InstrumentType::KIT && lastNotesState.count == 0
		    && currentNotesState.count == 1) {
			exitScaleModeOnButtonRelease = false;
			if (getCurrentClip()->inScaleMode) {
				instrumentClipView.setupChangingOfRootNote(currentNotesState.notes[0].note);
				requestRendering();
				displayCurrentScaleName();
			}
			else {
				enterScaleMode(currentNotesState.notes[0].note);
			}
		}
	}
	else {
		updateActiveNotes();
	}

	requestRendering();
	return ActionResult::DEALT_WITH;
}

void KeyboardScreen::evaluateActiveNotes() {
	lastNotesState = currentNotesState;
	layoutList[getCurrentClip()->keyboardState.currentLayout]->evaluatePads(pressedPads);
	currentNotesState = *layoutList[getCurrentClip()->keyboardState.currentLayout]->getNotesState();
}

void KeyboardScreen::updateActiveNotes() {
	Instrument* activeInstrument = getActiveInstrument();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	// Handle added notes
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		uint8_t newNote = currentNotesState.notes[idx].note;
		if (lastNotesState.noteEnabled(newNote)) {
			continue; // Note was enabled before
		}

		// Flash Song button if another clip with the same instrument is currently playing
		if (!clipIsActiveOnInstrument && currentNotesState.count > 0) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
		}

		// If note range menu is open and row is
		if (currentNotesState.count == 1 && !currentNotesState.notes[idx].generatedNote
		    && activeInstrument->type == InstrumentType::SYNTH) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(newNote
				                                              + ((SoundInstrument*)activeInstrument)->transpose);
			}
		}

		// Ensure the note the user is trying to sound isn't already sounding
		NoteRow* noteRow = ((InstrumentClip*)activeInstrument->activeClip)->getNoteRowForYNote(newNote);
		if (noteRow) {
			if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
				continue;
			}
		}

		// Actually sounding the note
		if (activeInstrument->type == InstrumentType::KIT) {
			unscrolledPadAudition(currentNotesState.notes[idx].velocity, newNote, false);
		}
		else {
			((MelodicInstrument*)activeInstrument)
			    ->beginAuditioningForNote(modelStack, newNote, currentNotesState.notes[idx].velocity,
			                              currentNotesState.notes[idx].mpeValues);
		}

		if (!currentNotesState.notes[idx].generatedNote) {
			drawNoteCode(newNote);
		}
		enterUIMode(UI_MODE_AUDITIONING);

		// Begin resampling - yup this is even allowed if we're in the card routine!
		if (Buttons::isButtonPressed(hid::button::RECORD) && audioRecorder.recordingSource == AudioInputChannel::NONE) {
			audioRecorder.beginOutputRecording();
			Buttons::recordButtonPressUsedUp = true;
		}

		// Recording - this only works *if* the Clip that we're viewing right now is the Instrument's activeClip
		if (activeInstrument->type != InstrumentType::KIT && clipIsActiveOnInstrument
		    && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(currentSong->currentClip)) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(currentSong->currentClip);

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) { // It definitely will be auditioning if we're here
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(0, NULL);
				((MelodicInstrument*)activeInstrument)
				    ->earlyNotes.insertElementIfNonePresent(newNote, currentNotesState.notes[idx].velocity,
				                                            getCurrentClip()->allowNoteTails(modelStackWithNoteRow));
			}

			else {
				Action* action = actionLogger.getNewAction(ACTION_RECORD, true);

				bool scaleAltered = false;

				ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentClip()->getOrCreateNoteRowForYNote(
				    newNote, modelStackWithTimelineCounter, action, &scaleAltered);
				NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
				if (thisNoteRow) {
					getCurrentClip()->recordNoteOn(modelStackWithNoteRow, currentNotesState.notes[idx].velocity);

					// If this caused the scale to change, update scroll
					if (action && scaleAltered) {
						action->updateYScrollClipViewAfter();
					}
				}
			}
		}
	}

	// Handle removed notes
	for (uint8_t idx = 0; idx < lastNotesState.count; ++idx) {
		uint8_t oldNote = lastNotesState.notes[idx].note;
		if (currentNotesState.noteEnabled(oldNote)) {
			continue;
		} // Note is still enabled

		NoteRow* noteRow = ((InstrumentClip*)activeInstrument->activeClip)->getNoteRowForYNote(oldNote);
		if (noteRow) {
			if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
				continue; // Note was activated by sequence
			}
		}

		if (activeInstrument->type == InstrumentType::KIT) {
			unscrolledPadAudition(0, oldNote, false);
		}
		else {
			((MelodicInstrument*)activeInstrument)->endAuditioningForNote(modelStack, oldNote);
		}

		// Recording - this only works *if* the Clip that we're viewing right now is the Instrument's activeClip
		if (activeInstrument->type != InstrumentType::KIT && clipIsActiveOnInstrument
		    && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(currentSong->currentClip)) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(currentSong->currentClip);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getCurrentClip()->getNoteRowForYNote(oldNote, modelStackWithTimelineCounter);
			if (modelStackWithNoteRow->getNoteRowAllowNull()) {
				getCurrentClip()->recordNoteOff(modelStackWithNoteRow);
			}
		}
	}

	if (lastNotesState.count != 0 && currentNotesState.count == 0) {
		exitUIMode(UI_MODE_AUDITIONING);

#if HAVE_OLED
		OLED::removePopup();
#else
		redrawNumericDisplay();
#endif
	}
}

ActionResult KeyboardScreen::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// Scale mode button
	if (b == SCALE_MODE) {
		if (getActiveInstrument()->type == InstrumentType::KIT) {
			return ActionResult::DEALT_WITH; // Kits can't do scales!
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

			// If user is auditioning just one note, we can go directly into Scale Mode and set that root note
			else if (currentUIMode == UI_MODE_AUDITIONING && currentNotesState.count == 1
			         && !getCurrentClip()->inScaleMode) {
				exitAuditionMode();
				enterScaleMode(currentNotesState.notes[0].note);
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
		if (on) { // Reset used flag on key up
			keyboardButtonUsed = false;
		}
		// Store active flag
		keyboardButtonActive = on;
		if (currentUIMode == UI_MODE_NONE && !keyboardButtonActive
		    && !keyboardButtonUsed) { // Leave if key up and not used
			instrumentClipView.recalculateColours();
			changeRootUI(&instrumentClipView);
			keyboardButtonUsed = false;
		}
	}

	// Song view button
	else if (b == SESSION_VIEW && on && currentUIMode == UI_MODE_NONE) {
		// Transition back to arranger
		if (currentSong->lastClipInstanceEnteredStartPos != -1 || currentSong->currentClip->section == 255) {
			if (arrangerView.transitionToArrangementEditor()) {
				return ActionResult::DEALT_WITH;
			}
		}

		// Transition back to clip
		currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;
		int transitioningToRow = sessionView.getClipPlaceOnScreen(currentSong->currentClip);
		memcpy(&PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));
		memcpy(&PadLEDs::occupancyMaskStore, PadLEDs::occupancyMask, sizeof(PadLEDs::occupancyMask));
		//memset(PadLEDs::occupancyMaskStore, 16, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));
		PadLEDs::numAnimatedRows = kDisplayHeight;
		for (int y = 0; y < kDisplayHeight; y++) {
			PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
			PadLEDs::animatedRowGoingFrom[y] = y;
		}

		PadLEDs::setupInstrumentClipCollapseAnimation(true);
		PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
		PadLEDs::renderClipExpandOrCollapse();
	}

	// Kit button
	else if (b == KIT) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (Buttons::isNewOrShiftButtonPressed()) {
				createNewInstrument(InstrumentType::KIT);
			}
			else {
				changeInstrumentType(InstrumentType::KIT);
			}
			selectLayout(0);
		}
	}

	else if (b == SYNTH) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (Buttons::isNewOrShiftButtonPressed()) {
				createNewInstrument(InstrumentType::SYNTH);
			}
			else {
				changeInstrumentType(InstrumentType::SYNTH);
			}
			selectLayout(0);
		}
	}

	else if (b == MIDI) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeInstrumentType(InstrumentType::MIDI_OUT);
		}
		selectLayout(0);
	}

	else if (b == CV) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeInstrumentType(InstrumentType::CV);
		}
		selectLayout(0);
	}

	else {
		requestRendering();
		ActionResult result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}

		// This might potentially do something while inCardRoutine but the condition above discards the call anyway
		return view.buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult KeyboardScreen::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	if (Buttons::isShiftButtonPressed() && currentUIMode == UI_MODE_NONE) {
		getCurrentClip()->colourOffset += offset;
		layoutList[getCurrentClip()->keyboardState.currentLayout]->precalculate();
	}
	else {
		layoutList[getCurrentClip()->keyboardState.currentLayout]->handleVerticalEncoder(offset);
		if (isUIModeWithinRange(padActionUIModes)) {
			evaluateActiveNotes();
			updateActiveNotes();
		}
	}

	requestRendering();
	return ActionResult::DEALT_WITH;
}

ActionResult KeyboardScreen::horizontalEncoderAction(int offset) {

	layoutList[getCurrentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(
	    offset, (Buttons::isShiftButtonPressed() && isUIModeWithinRange(padActionUIModes)));

	if (isUIModeWithinRange(padActionUIModes)) {
		evaluateActiveNotes();
		updateActiveNotes();
	}

	requestRendering();
	return ActionResult::DEALT_WITH;
}

void KeyboardScreen::selectLayout(int8_t offset) {
	KeyboardLayoutType lastLayout = getCurrentClip()->keyboardState.currentLayout;

	int32_t nextLayout = getCurrentClip()->keyboardState.currentLayout + offset;

	uint32_t searchCount = 0;
	while (searchCount < KeyboardLayoutType::MaxElement) {
		if (nextLayout < 0) {
			nextLayout = KeyboardLayoutType::MaxElement - 1;
		}
		if (nextLayout >= KeyboardLayoutType::MaxElement) {
			nextLayout = 0;
		}

		if (getActiveInstrument()->type == InstrumentType::KIT && layoutList[nextLayout]->supportsKit()) {
			break;
		}
		else if (getActiveInstrument()->type != InstrumentType::KIT && layoutList[nextLayout]->supportsInstrument()) {
			break;
		}

		++nextLayout;
		++searchCount;
	}

	if (searchCount >= KeyboardLayoutType::MaxElement) {
		nextLayout = 0;
	}

	getCurrentClip()->keyboardState.currentLayout = (KeyboardLayoutType)nextLayout;
	if (getCurrentClip()->keyboardState.currentLayout != lastLayout) {
		numericDriver.displayPopup(layoutList[getCurrentClip()->keyboardState.currentLayout]->name());
	}

	// Ensure scroll values are calculated in bounds
	layoutList[getCurrentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(0, false);

	// Precalculate because changing instruments can change pad colors
	layoutList[getCurrentClip()->keyboardState.currentLayout]->precalculate();
	requestRendering();
}

void KeyboardScreen::selectEncoderAction(int8_t offset) {
	if (keyboardButtonActive) {
		keyboardButtonUsed = true;
		selectLayout(offset);
	}
	else {
		InstrumentClipMinder::selectEncoderAction(offset);
		// Ensure scroll values are calculated in bounds
		layoutList[getCurrentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(0, false);
		layoutList[getCurrentClip()->keyboardState.currentLayout]->precalculate();
		requestRendering();
	}
}

void KeyboardScreen::exitAuditionMode() {
	memset(&pressedPads, 0, sizeof(pressedPads));
	evaluateActiveNotes();
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
	keyboardButtonUsed = true; // Ensure we don't leave the mode on button up
	InstrumentClipMinder::focusRegained();
	setLedStates();
}

void KeyboardScreen::openedInBackground() {
	getCurrentClip()->onKeyboardScreen = true;
	selectLayout(0); // Make sure we get a valid layout from the loaded file

	// Ensure scroll values are calculated in bounds
	layoutList[getCurrentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(0, false);
	layoutList[getCurrentClip()->keyboardState.currentLayout]->precalculate();
	requestRendering(); // This one originally also included sidebar, the other ones didn't
}

bool KeyboardScreen::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	memset(image, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth) * 3);
	memset(occupancyMask, 64,
	       sizeof(uint8_t) * kDisplayHeight
	           * (kDisplayWidth + kSideBarWidth)); // We assume the whole screen is occupied

	layoutList[getCurrentClip()->keyboardState.currentLayout]->renderPads(image);

	return true;
}

bool KeyboardScreen::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	layoutList[getCurrentClip()->keyboardState.currentLayout]->renderSidebarPads(image);

	return true;
}

void KeyboardScreen::flashDefaultRootNote() {
	uiTimerManager.setTimer(TIMER_DEFAULT_ROOT_NOTE, kFlashTime);
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	requestRendering();
}

void KeyboardScreen::enterScaleMode(int selectedRootNote) {
	getCurrentClip()->yScroll = instrumentClipView.setupForEnteringScaleMode(selectedRootNote);

	displayCurrentScaleName();

	evaluateActiveNotes();
	updateActiveNotes();

	requestRendering();
	setLedStates();
}

void KeyboardScreen::exitScaleMode() {
	getCurrentClip()->yScroll += instrumentClipView.setupForExitingScaleMode();

	evaluateActiveNotes();
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

	if (getActiveInstrument()->type != InstrumentType::KIT) {
		drawActualNoteCode(noteCode);
	}
}

bool KeyboardScreen::getAffectEntire() {
	return getCurrentClip()->affectEntire;
}

void KeyboardScreen::unscrolledPadAudition(int velocity, int note, bool shiftButtonDown) {
	// Ideally evaluateActiveNotes and InstrumentClipView::auditionPadAction should be harmonized
	// (even in the original keyboard_screen most of the non kit sounding was a copy from auditionPadAction)
	// but this refactor needs to wait for another day.
	// Until then we set the scroll to 0 during the auditioning
	int yScrollBackup = getCurrentClip()->yScroll;
	getCurrentClip()->yScroll = 0;
	instrumentClipView.auditionPadAction(velocity, note, shiftButtonDown);
	getCurrentClip()->yScroll = yScrollBackup;
}

uint8_t keyboardTickSquares[kDisplayHeight] = {255, 255, 255, 255, 255, 255, 255, 255};
const uint8_t keyboardTickColoursBasicRecording[kDisplayHeight] = {0, 0, 0, 0, 0, 0, 0, 0};
const uint8_t keyboardTickColoursLinearRecording[kDisplayHeight] = {0, 0, 0, 0, 0, 0, 0, 2};

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
		                * kDisplayWidth / currentSong->currentClip->loopLength;
		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}

		if (currentSong->currentClip->getCurrentlyRecordingLinearly()) {
			colours = keyboardTickColoursLinearRecording;
		}
	}

	keyboardTickSquares[kDisplayHeight - 1] = newTickSquare;

	PadLEDs::setTickSquares(keyboardTickSquares, colours);
}

} // namespace keyboard
