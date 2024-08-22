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
#include "extern.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"
#include <cstring>

#include "gui/ui/keyboard/layout.h"
#include "gui/ui/keyboard/layout/chord_keyboard.h"
#include "gui/ui/keyboard/layout/in_key.h"
#include "gui/ui/keyboard/layout/isomorphic.h"
#include "gui/ui/keyboard/layout/norns.h"
#include "gui/ui/keyboard/layout/velocity_drums.h"

deluge::gui::ui::keyboard::KeyboardScreen keyboardScreen{};

namespace deluge::gui::ui::keyboard {

layout::KeyboardLayoutIsomorphic keyboardLayoutIsomorphic{};
layout::KeyboardLayoutVelocityDrums keyboardLayoutVelocityDrums{};
layout::KeyboardLayoutInKey keyboardLayoutInKey{};
layout::KeyboardLayoutChord keyboardLayoutChord{};
layout::KeyboardLayoutNorns keyboardLayoutNorns{};
KeyboardLayout* layoutList[KeyboardLayoutType::KeyboardLayoutTypeMaxElement + 1] = {0};

KeyboardScreen::KeyboardScreen() {
	layoutList[KeyboardLayoutType::KeyboardLayoutTypeIsomorphic] = (KeyboardLayout*)&keyboardLayoutIsomorphic;
	layoutList[KeyboardLayoutType::KeyboardLayoutTypeDrums] = (KeyboardLayout*)&keyboardLayoutVelocityDrums;
	layoutList[KeyboardLayoutType::KeyboardLayoutTypeInKey] = (KeyboardLayout*)&keyboardLayoutInKey;
	layoutList[KeyboardLayoutType::KeyboardLayoutTypeChords] = (KeyboardLayout*)&keyboardLayoutChord;
	layoutList[KeyboardLayoutType::KeyboardLayoutTypeNorns] = (KeyboardLayout*)&keyboardLayoutNorns;

	memset(&pressedPads, 0, sizeof(pressedPads));
	currentNotesState = {0};
	lastNotesState = {0};
}

static const uint32_t padActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN,
                                            0}; // Careful - this is referenced in two places // I'm always careful ;)

void KeyboardScreen::killColumnSwitchKey(int32_t column) {
	if (column != kDisplayWidth && column != kDisplayWidth + 1) {
		return;
	}
	for (auto& pressedPad : pressedPads) {
		// kill the pad so it doesn't get used on release or hold
		if (pressedPad.x == column && pressedPad.y == 7) {
			pressedPad.dead = true;
			return;
		}
	}
}
ActionResult KeyboardScreen::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow some of the time when in card routine.
	}

	// Handle overruling shortcut presses
	ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, velocity);
	if (soundEditorResult != ActionResult::NOT_DEALT_WITH) {
		return soundEditorResult;
	}

	int32_t markDead = -1;

	// Pad pressed down, add to list if not full
	if (velocity) {
		// TODO: Logic should be inverted as part of a bigger rewrite
		if (currentUIMode == UI_MODE_IMPLODE_ANIMATION || currentUIMode == UI_MODE_ANIMATION_FADE
		    || currentUIMode == UI_MODE_INSTRUMENT_CLIP_COLLAPSING) {
			return ActionResult::DEALT_WITH;
		}

		int32_t freeSlotIdx = -1;
		for (int32_t idx = 0; idx < kMaxNumKeyboardPadPresses; ++idx) {
			// Free slot found
			if (!pressedPads[idx].active) {
				freeSlotIdx = idx;
				continue;
			}

			// Pad was already active
			if (pressedPads[idx].active && pressedPads[idx].x == x && pressedPads[idx].y == y) {
				freeSlotIdx = -1; // If a free slot was found previously, reset it so we don't write a second entry
				if ((AudioEngine::audioSampleTimer - pressedPads[idx].timeLastPadPress) > FlashStorage::holdTime) {
					pressedPads[idx].padPressHeld = true;
				}
				break;
			}
		}

		// Store active press in the free slot
		if (freeSlotIdx != -1) {
			pressedPads[freeSlotIdx].x = x;
			pressedPads[freeSlotIdx].y = y;
			pressedPads[freeSlotIdx].active = true;
			pressedPads[freeSlotIdx].dead = false;
			pressedPads[freeSlotIdx].padPressHeld = false;
			pressedPads[freeSlotIdx].timeLastPadPress = AudioEngine::audioSampleTimer;
		}
	}

	// Pad released, remove from list
	else {
		for (int32_t idx = 0; idx < kMaxNumKeyboardPadPresses; ++idx) {
			// Pad was already active
			if (pressedPads[idx].active && pressedPads[idx].x == x && pressedPads[idx].y == y) {
				pressedPads[idx].active = false;
				markDead = idx;
				if ((AudioEngine::audioSampleTimer - pressedPads[idx].timeLastPadPress) > FlashStorage::holdTime) {
					pressedPads[idx].padPressHeld = true;
				}
				break;
			}
		}
	}

	evaluateActiveNotes();

	if (markDead != -1) {
		pressedPads[markDead].dead = true;
	}

	// Handle setting root note
	if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// We probably couldn't have got this far if it was a Kit, but let's just check
		if (getCurrentOutputType() != OutputType::KIT && lastNotesState.count == 0 && currentNotesState.count == 1) {
			toggleScaleModeOnButtonRelease = false;
			if (getCurrentInstrumentClip()->inScaleMode) {
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
	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->evaluatePads(pressedPads);
	currentNotesState = layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->getNotesState();
}

void KeyboardScreen::updateActiveNotes() {
	Instrument* activeInstrument = getCurrentInstrument();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	// Map from the current index of a note to its index in the previous note state.
	//
	// -1 if the note was not present in the previous
	std::array<size_t, kMaxNumActiveNotes> currentToLastIdx;
	{
		size_t currentIdx;
		for (currentIdx = 0; currentIdx < currentNotesState.count; ++currentIdx) {
			auto& currentNote = currentNotesState.notes[currentIdx];
			size_t lastIdx;

			// Minor optimization: if we know the note can't be enabled, immediately write -1 and go next
			if (!lastNotesState.noteEnabled(currentNote.note)) {
				currentToLastIdx[currentIdx] = -1;
				continue;
			}

			static_assert(kMaxNumActiveNotes < 12, "N2 loop below, performance will suffer");
			for (lastIdx = 0; lastIdx < lastNotesState.count; ++lastIdx) {
				auto& lastNote = lastNotesState.notes[lastIdx];

				if (lastNote.note == currentNote.note) {
					currentToLastIdx[currentIdx] = lastIdx;
					break;
				}
			}

			if (lastIdx == lastNotesState.count) {
				// Failed to find in previous notes
				currentToLastIdx[currentIdx] = -1;
			}
		}

		for (; currentIdx < currentToLastIdx.size(); ++currentIdx) {
			currentToLastIdx[currentIdx] = -1;
		}
	}

	// Send note-offs for things we're going to retrigger
	for (auto idx = 0; idx < currentNotesState.count; ++idx) {
		if (currentToLastIdx[idx] < 0) {
			// Note was not on in the last pass, can not require retriggering.
			continue;
		}
		auto& currentNote = currentNotesState.notes[idx];
		auto& oldNote = lastNotesState.notes[currentToLastIdx[idx]];
		if (oldNote.activationCount < currentNote.activationCount) {
			// Retrigger required.
			noteOff(*modelStack, *activeInstrument, clipIsActiveOnInstrument, currentNote.note);
		}
	}

	// Handle added notes
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		auto& newNoteState = currentNotesState.notes[idx];
		auto newNote = newNoteState.note;

		if (currentToLastIdx[idx] == -1) {
			// This is a completely new note, handle stuff that we don't want to do for retriggers
			//
			// Flash Song button if another clip with the same instrument is currently playing
			if (!clipIsActiveOnInstrument && currentNotesState.count > 0) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
			}

			// If note range menu is open and row is
			if (currentNotesState.count == 1 && !currentNotesState.notes[idx].generatedNote
			    && activeInstrument->type == OutputType::SYNTH) {
				if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
					menu_item::multiRangeMenu.noteOnToChangeRange(newNote
					                                              + ((SoundInstrument*)activeInstrument)->transpose);
				}
			}
		}
		else {
			NoteState& oldNote = lastNotesState.notes[currentToLastIdx[idx]];
			if (oldNote.activationCount >= newNoteState.activationCount) {
				continue;
			}
		}

		// Ensure the note the user is trying to sound isn't already sounding
		NoteRow* noteRow = ((InstrumentClip*)activeInstrument->getActiveClip())->getNoteRowForYNote(newNote);
		if (noteRow) {
			if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
				continue;
			}
		}

		// Actually sounding the note
		if (activeInstrument->type == OutputType::KIT) {
			unscrolledPadAudition(currentNotesState.notes[idx].velocity, newNote, false);
		}
		else {
			((MelodicInstrument*)activeInstrument)
			    ->beginAuditioningForNote(modelStack, newNote, currentNotesState.notes[idx].velocity,
			                              currentNotesState.notes[idx].mpeValues);
		}

		// Post sound logic for non-retrigger events
		if (currentToLastIdx[idx] == -1) {
			if (!currentNotesState.notes[idx].generatedNote) {
				drawNoteCode(newNote);
			}
			enterUIMode(UI_MODE_AUDITIONING);

			// Begin resampling - yup this is even allowed if we're in the card routine!
			if (Buttons::isButtonPressed(deluge::hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}
		}

		// Recording - this only works *if* the Clip that we're viewing right now is the Instrument's activeClip
		if (activeInstrument->type != OutputType::KIT && clipIsActiveOnInstrument
		    && playbackHandler.shouldRecordNotesNow() && currentSong->isClipActive(getCurrentClip())
		    && getCurrentClip()->armedForRecording) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(getCurrentClip());

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) { // It definitely will be auditioning if we're here
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(0, NULL);
				((MelodicInstrument*)activeInstrument)
				    ->earlyNotes.insertElementIfNonePresent(
				        newNote, currentNotesState.notes[idx].velocity,
				        getCurrentInstrumentClip()->allowNoteTails(modelStackWithNoteRow));
			}

			else {
				Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

				bool scaleAltered = false;

				ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentInstrumentClip()->getOrCreateNoteRowForYNote(
				    newNote, modelStackWithTimelineCounter, action, &scaleAltered);
				NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
				if (thisNoteRow) {
					getCurrentInstrumentClip()->recordNoteOn(modelStackWithNoteRow,
					                                         currentNotesState.notes[idx].velocity);

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

		noteOff(*modelStack, *activeInstrument, clipIsActiveOnInstrument, oldNote);
	}

	if (lastNotesState.count != 0 && currentNotesState.count == 0) {
		exitUIMode(UI_MODE_AUDITIONING);

		if (display->haveOLED()) {
			deluge::hid::display::OLED::removePopup();
		}
		else {
			redrawNumericDisplay();
		}
	}
}

void KeyboardScreen::noteOff(ModelStack& modelStack, Instrument& activeInstrument, bool clipIsActiveOnInstrument,
                             int32_t note) {
	NoteRow* noteRow = (static_cast<InstrumentClip*>(activeInstrument.getActiveClip()))->getNoteRowForYNote(note);
	if (noteRow) {
		if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
			return; // Note was activated by sequence
		}
	}

	if (activeInstrument.type == OutputType::KIT) {
		unscrolledPadAudition(0, note, false);
	}
	else {
		(static_cast<MelodicInstrument*>(&activeInstrument))->endAuditioningForNote(&modelStack, note);
	}

	// Recording - this only works *if* the Clip that we're viewing right now is the Instrument's activeClip
	if (activeInstrument.type != OutputType::KIT && clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow()
	    && currentSong->isClipActive(getCurrentClip())) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack.addTimelineCounter(getCurrentClip());
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    getCurrentInstrumentClip()->getNoteRowForYNote(note, modelStackWithTimelineCounter);
		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			getCurrentInstrumentClip()->recordNoteOff(modelStackWithNoteRow);
		}
	}
}

ActionResult KeyboardScreen::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// Scale mode button
	if (b == SCALE_MODE) {
		if ((getCurrentOutputType() == OutputType::KIT)) {
			// Kits can't do scales!
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_KEYBOARD_VIEW_CANT_ENTER_SCALE));
			return ActionResult::DEALT_WITH;
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on) {
			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {

				// If user holding shift and we're already in scale mode, cycle through available scales
				if (Buttons::isShiftButtonPressed() && getCurrentInstrumentClip()->inScaleMode) {
					cycleThroughScales();
					requestRendering();
				}

				// Or, no shift button - normal behaviour
				else {
					currentUIMode = UI_MODE_SCALE_MODE_BUTTON_PRESSED;
					toggleScaleModeOnButtonRelease = true;
					// if (!getCurrentInstrumentClip()->inScaleMode) {
					// 	calculateDefaultRootNote(); // Calculate it now so we can show the user even before they've
					// released the button 	flashDefaultRootNoteOn = false; 	flashDefaultRootNote();
					// }
				}
			}

			// If user is auditioning just one note, we can go directly into Scale Mode and set that root note
			else if (currentUIMode == UI_MODE_AUDITIONING && currentNotesState.count == 1
			         && !getCurrentInstrumentClip()->inScaleMode) {
				exitAuditionMode();
				enterScaleMode(currentNotesState.notes[0].note);
			}
		}
		else {
			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				currentUIMode = UI_MODE_NONE;
				if (getCurrentInstrumentClip()->inScaleMode) {
					if (toggleScaleModeOnButtonRelease) {
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
			if (getCurrentClip()->onAutomationClipView) {
				changeRootUI(&automationView);
			}
			else {
				changeRootUI(&instrumentClipView);
			}

			keyboardButtonUsed = false;
		}
	}

	// Song view button
	else if (b == SESSION_VIEW && on && currentUIMode == UI_MODE_NONE) {
		// Transition back to arranger
		if (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->section == 255) {
			if (arrangerView.transitionToArrangementEditor()) {
				return ActionResult::DEALT_WITH;
			}
		}

		sessionView.transitionToSessionView();
	}

	// toggle UI to go back to after you exit keyboard mode between automation instrument clip view and regular
	// instrument clip view
	else if (b == CLIP_VIEW) {
		if (on) {
			if (getCurrentClip()->onAutomationClipView) {
				getCurrentClip()->onAutomationClipView = false;
				indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, true);
			}
			else {
				getCurrentClip()->onAutomationClipView = true;
				indicator_leds::blinkLed(IndicatorLED::CLIP_VIEW);
			}
		}
	}

	// Kit button
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		if (on) {
			bool result;
			if (Buttons::isShiftButtonPressed()) {
				result = createNewInstrument(OutputType::KIT);
			}
			else {
				result = changeOutputType(OutputType::KIT);
			}
			if (result) {
				selectLayout(0);
			}
		}
	}

	else if (b == SYNTH && currentUIMode == UI_MODE_NONE) {
		if (on) {
			bool result;
			if (Buttons::isButtonPressed(MOD7)) { // FM
				result = createNewInstrument(OutputType::SYNTH, true);
			}
			else if (Buttons::isShiftButtonPressed()) {
				result = createNewInstrument(OutputType::SYNTH);
			}
			else {
				result = changeOutputType(OutputType::SYNTH);
			}
			if (result) {
				selectLayout(0);
			}
		}
	}

	else if (b == MIDI && currentUIMode == UI_MODE_NONE) {
		if (on) {
			if (changeOutputType(OutputType::MIDI_OUT)) {
				selectLayout(0);
			}
		}
	}

	else if (b == CV && currentUIMode == UI_MODE_NONE) {
		if (on) {
			if (changeOutputType(OutputType::CV)) {
				selectLayout(0);
			}
		}
	}

	else if (b == SELECT_ENC && on && getCurrentInstrumentClip()->inScaleMode
	         && currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
		toggleScaleModeOnButtonRelease = false;
		cycleThroughScales();
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->precalculate();
		requestRendering();
	}

	// store if the user is holding the x encoder
	else if (b == X_ENC) {
		xEncoderActive = on;
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

ActionResult KeyboardScreen::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	if (Buttons::isShiftButtonPressed() && currentUIMode == UI_MODE_NONE) {
		getCurrentInstrumentClip()->colourOffset += offset;
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->precalculate();
	}
	else {
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->handleVerticalEncoder(offset);
		if (isUIModeWithinRange(padActionUIModes)) {
			evaluateActiveNotes();
			updateActiveNotes();
		}
	}

	requestRendering();
	return ActionResult::DEALT_WITH;
}

ActionResult KeyboardScreen::horizontalEncoderAction(int32_t offset) {

	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(
	    offset, (Buttons::isShiftButtonPressed() && isUIModeWithinRange(padActionUIModes)), pressedPads,
	    xEncoderActive);

	if (isUIModeWithinRange(padActionUIModes)) {
		evaluateActiveNotes();
		updateActiveNotes();
	}

	requestRendering();
	return ActionResult::DEALT_WITH;
}

void KeyboardScreen::selectLayout(int8_t offset) {
	KeyboardLayoutType lastLayout = getCurrentInstrumentClip()->keyboardState.currentLayout;

	int32_t nextLayout = getCurrentInstrumentClip()->keyboardState.currentLayout + offset;

	uint32_t searchCount = 0;
	while (searchCount < KeyboardLayoutType::KeyboardLayoutTypeMaxElement) {
		if (nextLayout < 0) {
			nextLayout = KeyboardLayoutType::KeyboardLayoutTypeMaxElement - 1;
		}
		if (nextLayout >= KeyboardLayoutType::KeyboardLayoutTypeMaxElement) {
			nextLayout = 0;
		}

		if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::DisplayNornsLayout) == RuntimeFeatureStateToggle::Off
		     && nextLayout == KeyboardLayoutType::KeyboardLayoutTypeNorns)
		    || (runtimeFeatureSettings.get(RuntimeFeatureSettingType::DisplayChordKeyboard)
		            == RuntimeFeatureStateToggle::Off
		        && nextLayout == KeyboardLayoutType::KeyboardLayoutTypeChords)) {
			// Don't check the next conditions, this one is already lost
		}
		else if (getCurrentOutputType() == OutputType::KIT && layoutList[nextLayout]->supportsKit()) {
			break;
		}
		else if (getCurrentOutputType() != OutputType::KIT && layoutList[nextLayout]->supportsInstrument()) {
			break;
		}

		// Offset is guaranteed to be -1, 0 or 1, see limitedDetentPos
		nextLayout += (offset == 0 ? 1 : offset);
		++searchCount;
	}

	if (searchCount >= KeyboardLayoutType::KeyboardLayoutTypeMaxElement) {
		nextLayout = 0;
	}

	getCurrentInstrumentClip()->keyboardState.currentLayout = (KeyboardLayoutType)nextLayout;
	if (getCurrentInstrumentClip()->keyboardState.currentLayout != lastLayout) {
		display->displayPopup(layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->name());
	}

	// Ensure scale mode is as expected
	if (getCurrentOutputType() != OutputType::KIT) {
		auto requiredScaleMode =
		    layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->requiredScaleMode();
		if (requiredScaleMode == RequiredScaleMode::Enabled && !getCurrentInstrumentClip()->inScaleMode) {
			getCurrentInstrumentClip()->yScroll =
			    instrumentClipView.setupForEnteringScaleMode(currentSong->key.rootNote);
			setLedStates();
		}
		else if (requiredScaleMode == RequiredScaleMode::Disabled) {
			getCurrentInstrumentClip()->yScroll += instrumentClipView.setupForExitingScaleMode();
			exitScaleMode();
			setLedStates();
		}
	}

	// Ensure scroll values are calculated in bounds
	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(0, false, pressedPads,
	                                                                                             xEncoderActive);

	// Precalculate because changing instruments can change pad colours
	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->precalculate();
	requestRendering();
}

void KeyboardScreen::selectEncoderAction(int8_t offset) {
	if (keyboardButtonActive) {
		keyboardButtonUsed = true;
		selectLayout(offset);
	}
	else if (getCurrentOutputType() != OutputType::KIT && currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED
	         && getCurrentInstrumentClip()->inScaleMode) {
		toggleScaleModeOnButtonRelease = false;
		int32_t newRootNote = ((currentSong->key.rootNote + kOctaveSize) + offset) % kOctaveSize;
		instrumentClipView.setupChangingOfRootNote(newRootNote);

		char noteName[3] = {0};
		noteName[0] = noteCodeToNoteLetter[newRootNote];
		if (display->haveOLED()) {
			if (noteCodeIsSharp[newRootNote]) {
				noteName[1] = '#';
			}
		}
		display->displayPopup(noteName, 3, false, (noteCodeIsSharp[newRootNote] ? 0 : 255));
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(
		    0, false, pressedPads, xEncoderActive);
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->precalculate();
		requestRendering();
	}
	else {
		InstrumentClipMinder::selectEncoderAction(offset);
		// Ensure scroll values are calculated in bounds
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(
		    0, false, pressedPads, xEncoderActive);
		layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->precalculate();
		requestRendering();
	}
}

void KeyboardScreen::exitAuditionMode() {
	memset(&pressedPads, 0, sizeof(pressedPads));
	evaluateActiveNotes();
	updateActiveNotes();

	exitUIMode(UI_MODE_AUDITIONING);
	if (display->have7SEG()) {
		redrawNumericDisplay();
	}
}

bool KeyboardScreen::opened() {
	focusRegained();
	openedInBackground();
	return true;
}

void KeyboardScreen::focusRegained() {
	keyboardButtonUsed = true; // Ensure we don't leave the mode on button up
	InstrumentClipMinder::focusRegained();
	setLedStates();

	selectLayout(0); // Make sure we get a valid layout from the loaded file
}

void KeyboardScreen::displayOrLanguageChanged() {
	InstrumentClipMinder::displayOrLanguageChanged();
}

void KeyboardScreen::openedInBackground() {
	getCurrentInstrumentClip()->onKeyboardScreen = true;

	// Ensure scroll values are calculated in bounds
	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->handleHorizontalEncoder(0, false, pressedPads,
	                                                                                             xEncoderActive);
	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->precalculate();
	requestRendering(); // This one originally also included sidebar, the other ones didn't
}

void KeyboardScreen::checkNewInstrument(Instrument* newInstrument) {
	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->checkNewInstrument(newInstrument);
}

bool KeyboardScreen::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	PadLEDs::renderingLock = true;

	// We assume the whole screen is occupied
	memset(occupancyMask, 64, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth));

	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->renderPads(image);

	PadLEDs::renderingLock = false;

	return true;
}

bool KeyboardScreen::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->renderSidebarPads(image);

	return true;
}

void KeyboardScreen::flashDefaultRootNote() {
	uiTimerManager.setTimer(TimerName::DEFAULT_ROOT_NOTE, kFlashTime);
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	requestRendering();
}

void KeyboardScreen::enterScaleMode(int32_t selectedRootNote) {
	auto requiredScaleMode = layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->requiredScaleMode();
	if (requiredScaleMode == RequiredScaleMode::Disabled) {
		return;
	}

	getCurrentInstrumentClip()->yScroll = instrumentClipView.setupForEnteringScaleMode(selectedRootNote);

	displayCurrentScaleName();

	evaluateActiveNotes();
	updateActiveNotes();

	requestRendering();
	setLedStates();
}

void KeyboardScreen::exitScaleMode() {
	auto requiredScaleMode = layoutList[getCurrentInstrumentClip()->keyboardState.currentLayout]->requiredScaleMode();
	if (requiredScaleMode == RequiredScaleMode::Enabled) {
		return;
	}

	getCurrentInstrumentClip()->yScroll += instrumentClipView.setupForExitingScaleMode();

	evaluateActiveNotes();
	updateActiveNotes();

	requestRendering();
	setLedStates();
}

void KeyboardScreen::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
	InstrumentClipMinder::setLedStates();
}

void KeyboardScreen::drawNoteCode(int32_t noteCode) {
	// Might not want to actually do this...
	if (!getCurrentUI()->toClipMinder()) {
		return;
	}

	if (getCurrentOutputType() != OutputType::KIT) {
		drawActualNoteCode(noteCode);
	}
}

bool KeyboardScreen::getAffectEntire() {
	return getCurrentInstrumentClip()->affectEntire;
}

void KeyboardScreen::unscrolledPadAudition(int32_t velocity, int32_t note, bool shiftButtonDown) {
	// Ideally evaluateActiveNotes and InstrumentClipView::auditionPadAction should be harmonized
	// (even in the original keyboard_screen most of the non kit sounding was a copy from auditionPadAction)
	// but this refactor needs to wait for another day.
	// Until then we set the scroll to 0 during the auditioning
	int32_t yScrollBackup = getCurrentInstrumentClip()->yScroll;
	getCurrentInstrumentClip()->yScroll = trunc(note / 8) * 8;
	instrumentClipView.auditionPadAction(velocity, note % 8, shiftButtonDown);
	getCurrentInstrumentClip()->yScroll = yScrollBackup;
}

uint8_t keyboardTickSquares[kDisplayHeight] = {255, 255, 255, 255, 255, 255, 255, 255};
const uint8_t keyboardTickColoursBasicRecording[kDisplayHeight] = {0, 0, 0, 0, 0, 0, 0, 0};
const uint8_t keyboardTickColoursLinearRecording[kDisplayHeight] = {0, 0, 0, 0, 0, 0, 0, 2};

void KeyboardScreen::graphicsRoutine() {
	int32_t newTickSquare;

	const uint8_t* colours = keyboardTickColoursBasicRecording;

	if (!playbackHandler.isEitherClockActive() || !playbackHandler.isCurrentlyRecording()
	    || !currentSong->isClipActive(getCurrentClip()) || currentUIMode == UI_MODE_EXPLODE_ANIMATION
	    || currentUIMode == UI_MODE_IMPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn) {
		newTickSquare = 255;
	}
	else {
		newTickSquare = (uint64_t)(getCurrentClip()->lastProcessedPos
		                           + playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick())
		                * kDisplayWidth / getCurrentClip()->loopLength;
		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}

		if (getCurrentClip()->getCurrentlyRecordingLinearly()) {
			colours = keyboardTickColoursLinearRecording;
		}
	}

	keyboardTickSquares[kDisplayHeight - 1] = newTickSquare;

	PadLEDs::setTickSquares(keyboardTickSquares, colours);
}

} // namespace deluge::gui::ui::keyboard
