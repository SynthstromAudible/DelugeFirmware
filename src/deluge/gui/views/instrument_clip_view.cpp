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

#include "gui/views/instrument_clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/timeline_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_engine.h"
#include "lib/printf.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/drum/drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multi_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <limits>
#include <new>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge::gui;

InstrumentClipView instrumentClipView{};

InstrumentClipView::InstrumentClipView() {

	numEditPadPresses = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		editPadPresses[i].isActive = false;
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		numEditPadPressesPerNoteRowOnScreen[yDisplay] = 0;
		lastAuditionedVelocityOnScreen[yDisplay] = 255;
		auditionPadIsPressed[yDisplay] = 0;
	}

	auditioningSilently = false;
	timeLastEditPadPress = 0;
	//newDrumOptionSelected = false;
	firstCopiedNoteRow = NULL;
}

bool InstrumentClipView::opened() {

	openedInBackground();

	InstrumentClipMinder::opened();

	focusRegained();

	return true;
}

void InstrumentClipView::openedInBackground() {
	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);

	recalculateColours();

	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	AudioEngine::logAction("InstrumentClipView::beginSession 2");

	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
		               true);
		renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}
	getCurrentInstrumentClip()->onKeyboardScreen = false;
	getCurrentClip()->onAutomationClipView = false;
}

// Initializes some stuff to begin a new editing session
void InstrumentClipView::focusRegained() {
	ClipView::focusRegained();

	auditioningSilently = false; // Necessary?

	InstrumentClipMinder::focusRegained();

	setLedStates();
}

void InstrumentClipView::displayOrLanguageChanged() {
	InstrumentClipMinder::displayOrLanguageChanged();
}

void InstrumentClipView::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);
	InstrumentClipMinder::setLedStates();
}

ActionResult InstrumentClipView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Scale mode button
	if (b == SCALE_MODE) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Kits can't do scales!
		if (getCurrentOutputType() == OutputType::KIT) {
			if (on) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::KIT);
			}
			return ActionResult::DEALT_WITH;
		}

		actionLogger.deleteAllLogs(); // Can't undo past this!

		if (on) {

			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {

				// If user holding shift and we're already in scale mode, cycle through available scales
				if (Buttons::isShiftButtonPressed() && getCurrentInstrumentClip()->inScaleMode) {
					cycleThroughScales();
					recalculateColours();
					uiNeedsRendering(this);
					// Hook point for specificMidiDevice
					iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_CHANGE_SCALE);
				}

				// Or, no shift button - normal behaviour
				else {
					currentUIMode = UI_MODE_SCALE_MODE_BUTTON_PRESSED;
					exitScaleModeOnButtonRelease = true;
					if (!getCurrentInstrumentClip()->inScaleMode) {
						calculateDefaultRootNote(); // Calculate it now so we can show the user even before they've released the button
						flashDefaultRootNoteOn = false;
						flashDefaultRootNote();
						// Hook point for specificMidiDevice
						iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_CHANGE_SCALE);
					}
				}
			}

			// If user is auditioning just one NoteRow, we can go directly into Scale Mode and set that root note
			else if (oneNoteAuditioning() && !getCurrentInstrumentClip()->inScaleMode) {
				cancelAllAuditioning();
				enterScaleMode(lastAuditionedYDisplay);
			}
		}
		else {
			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				currentUIMode = UI_MODE_NONE;
				if (getCurrentInstrumentClip()->inScaleMode) {
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

	// Song view button
	else if (b == SESSION_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}
			else {
doOther:
				sessionView.transitionToSessionView();
			}
		}
	}

	// Clip view button
	else if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&automationClipView);
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&keyboardScreen);
		}
	}

	// Wrap edit button
	else if (b == CROSS_SCREEN_EDIT) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (getCurrentInstrumentClip()->wrapEditing) {
					getCurrentInstrumentClip()->wrapEditing = false;
				}
				else {
					getCurrentInstrumentClip()->wrapEditLevel = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;
					// Ensure that there are actually multiple screens to edit across
					if (getCurrentInstrumentClip()->wrapEditLevel < getCurrentClip()->loopLength) {
						getCurrentInstrumentClip()->wrapEditing = true;
					}
				}

				setLedStates();
			}
		}
	}

	// Record button if holding audition pad
	else if (b == RECORD && (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING)) {
		if (on && getCurrentOutputType() == OutputType::KIT && audioRecorder.recordingSource == AudioInputChannel::NONE
		    && (!playbackHandler.isEitherClockActive() || !playbackHandler.ticksLeftInCountIn)) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {

				currentUIMode = UI_MODE_NONE;

				// Make a new NoteRow
				int32_t noteRowIndex;
				NoteRow* newNoteRow = createNewNoteRowForKit(modelStack, yDisplayOfNewNoteRow, &noteRowIndex);
				if (newNoteRow) {
					uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);

					int32_t noteRowId = getCurrentInstrumentClip()->getNoteRowId(newNoteRow, noteRowIndex);
					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, newNoteRow);

					enterDrumCreator(modelStackWithNoteRow, true);
				}
			}

			else if (currentUIMode == UI_MODE_AUDITIONING) {
				cutAuditionedNotesToOne();

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentInstrumentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack);

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
				if (noteRow->drum) {
					noteRow->drum->drumWontBeRenderedForAWhile();
				}
				cancelAllAuditioning();

				enterDrumCreator(modelStackWithNoteRow, true);
			}
		}
	}

	// Back button if adding Drum
	else if (b == BACK && currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			currentUIMode = UI_MODE_NONE;
			if (display->have7SEG()) {
				InstrumentClipMinder::redrawNumericDisplay();
			}
			uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);
		}
	}

	// Load / Kit button if creating new NoteRow for Drum
	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW && ((b == LOAD) || (b == KIT))) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			currentUIMode = UI_MODE_NONE;

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// Make a new NoteRow
			int32_t noteRowIndex;
			NoteRow* newNoteRow = createNewNoteRowForKit(modelStack, yDisplayOfNewNoteRow, &noteRowIndex);
			if (!newNoteRow) {
				display->displayError(ERROR_INSUFFICIENT_RAM);
				return ActionResult::DEALT_WITH;
			}

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, newNoteRow);

			enterDrumCreator(modelStackWithNoteRow, false);

			uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);
		}
	}

	// Load / kit button if auditioning
	else if (currentUIMode == UI_MODE_AUDITIONING && ((b == LOAD) || (b == KIT))
	         && (!playbackHandler.isEitherClockActive() || !playbackHandler.ticksLeftInCountIn)) {

		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// Auditioning drum
			if (getCurrentOutputType() == OutputType::KIT) {
				cutAuditionedNotesToOne();
				int32_t noteRowIndex;
				NoteRow* noteRow =
				    getCurrentInstrumentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, currentSong, &noteRowIndex);
				cancelAllAuditioning();
				if (noteRow->drum) {
					noteRow->drum->drumWontBeRenderedForAWhile();
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithNoteRow* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory)->addNoteRow(noteRowIndex, noteRow);

				enterDrumCreator(modelStack, false);
			}

			// Auditioning synth
			if (getCurrentOutputType() == OutputType::SYNTH) {
				cancelAllAuditioning();

				// Can't fail because we just set the selected Drum
				bool success = soundEditor.setup(getCurrentInstrumentClip(), &menu_item::fileSelectorMenu, 0);
				if (success) {
					openUI(&soundEditor);
				}
			}
		}
	}

	// Kit button. Unlike the other instrument-type buttons, whose code is in InstrumentClipMinder, this one is only allowed in the InstrumentClipView
	else if (b == KIT && currentUIMode == UI_MODE_NONE) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isNewOrShiftButtonPressed()) {
				createNewInstrument(OutputType::KIT);
			}
			else {
				changeOutputType(OutputType::KIT);
			}
		}
	}

	else if (b == SYNTH && currentUIMode != UI_MODE_HOLDING_SAVE_BUTTON
	         && currentUIMode != UI_MODE_HOLDING_LOAD_BUTTON) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				if (Buttons::isNewOrShiftButtonPressed()) {
					createNewInstrument(OutputType::SYNTH);
				}
				else {
					changeOutputType(OutputType::SYNTH);
				}
			}
			else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
				//hook to load synth preset
				createDrumForAuditionedNoteRow(DrumType::SOUND);
			}
		}
	}

	else if (b == MIDI) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				changeOutputType(OutputType::MIDI_OUT);
			}
			else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
				createDrumForAuditionedNoteRow(DrumType::MIDI);
			}
		}
	}

	else if (b == CV) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_NONE) {
				changeOutputType(OutputType::CV);
			}
			else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW || currentUIMode == UI_MODE_AUDITIONING) {
				createDrumForAuditionedNoteRow(DrumType::GATE);
			}
		}
	}

	// Save / delete button if NoteRow held down
	else if (b == SAVE && currentUIMode == UI_MODE_NOTES_PRESSED) {
		InstrumentClip* clip = getCurrentInstrumentClip();

		if (on && numEditPadPresses == 1 && getCurrentOutputType() == OutputType::KIT && clip->getNumNoteRows() >= 2) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive) {

					int32_t yDisplay = editPadPresses[i].yDisplay;

					endEditPadPress(i);
					checkIfAllEditPadPressesEnded(false);
					reassessAuditionStatus(yDisplay);

					int32_t noteRowIndex = yDisplay + clip->yScroll;

					if (ALPHA_OR_BETA_VERSION
					    && (noteRowIndex < 0 || noteRowIndex >= clip->noteRows.getNumElements())) {
						FREEZE_WITH_ERROR("E323");
					}

					if (clip->isActiveOnOutput()) {
						NoteRow* noteRow = clip->noteRows.getElement(noteRowIndex);
						if (noteRow->drum) {
							noteRow->drum->drumWontBeRenderedForAWhile();
						}
					}

					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithTimelineCounter* modelStack =
					    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
					clip->deleteNoteRow(modelStack, noteRowIndex);

					// Note: I should fix this - if deleting a NoteRow of a MIDI drum that we're auditioning via MIDI, this will leave a stuck note...

					// If NoteRow was bottom half of screen...
					if (yDisplay < (kDisplayHeight >> 1)) {
						if (!noteRowIndex || clip->noteRows.getNumElements() >= (kDisplayHeight >> 1)) {
							clip->yScroll--;
						}
					}

					// Or top half of screen...
					else {
						if (!noteRowIndex && clip->noteRows.getNumElements() < (kDisplayHeight >> 1)) {
							clip->yScroll--;
						}
					}

					actionLogger.deleteAllLogs(); // Can't undo past this

					setSelectedDrum(NULL, true);

					recalculateColours();
					uiNeedsRendering(this);

					// Can't remember why repopulateNoteRowsOnScreen() doesn't do the sidebar automatically?

					currentUIMode = UI_MODE_NONE;

					AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

					break;
				}
			}
		}
	}

	// Kit + Shift + Save/Delete: shorcut that will delete all Kit rows that does not contain notes
	// (instead of pressing Note + Delete to do it one by one)
	else if (b == SAVE && currentUIMode != UI_MODE_NOTES_PRESSED && Buttons::isShiftButtonPressed()
	         && Buttons::isButtonPressed(KIT) && getCurrentOutputType() == OutputType::KIT
	         && (runtimeFeatureSettings.get(RuntimeFeatureSettingType::DeleteUnusedKitRows)
	             == RuntimeFeatureStateToggle::On)) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			InstrumentClip* clip = getCurrentInstrumentClip();

			if (!clip->containsAnyNotes()) {
				display->displayPopup(
				    deluge::l10n::get(deluge::l10n::String::STRING_FOR_AT_LEAST_ONE_ROW_NEEDS_TO_HAVE_NOTES));
			}
			else {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				int32_t i;
				for (i = clip->noteRows.getNumElements() - 1; i >= 0; i--) {
					NoteRow* noteRow = clip->noteRows.getElement(i);
					if (noteRow->hasNoNotes() && clip->noteRows.getNumElements() > 1) {
						// If the row has not notes and is not the last one
						clip->deleteNoteRow(modelStack, i);
					}
				}

				clip->yScroll = 0; // Reset scroll position

				actionLogger.deleteAllLogs(); // Can't undo past this

				setSelectedDrum(NULL, true);

				recalculateColours();
				uiNeedsRendering(this);

				// Show popup to make it clear what just happened
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DELETED_UNUSED_ROWS));
			}
		}
	}

	// Horizontal encoder button if learn button pressed. Make sure you let the "off" action slide past to the Editor
	else if (b == X_ENC && on && Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (Buttons::isShiftButtonPressed()) {
			pasteNotes(true);
		}
		else {
			copyNotes();
		}
	}
	else if (b == TEMPO_ENC && isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)
	         && runtimeFeatureSettings.get(RuntimeFeatureSettingType::Quantize) == RuntimeFeatureStateToggle::On) {
		//prevent Tempo pop-up , when note is pressed
	}
	// Horizontal encoder button
	else if (b == X_ENC) {

		// If user wants to "multiple" Clip contents
		if (on && Buttons::isShiftButtonPressed() && !isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			if (isNoUIModeActive()) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				if (Buttons::isButtonPressed(deluge::hid::button::CROSS_SCREEN_EDIT)) {
					pasteNotes(false);
				}
				else {
					// Zoom to max if we weren't already there...

					if (!zoomToMax()) {
						// Or if we didn't need to do that, double Clip length
						doubleClipLengthAction();
					}
					else {
						displayZoomLevel();
					}
				}
			}
			// Whether or not we did the "multiply" action above, we need to be in this UI mode, e.g. for rotating individual NoteRow
			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}

		// Otherwise...
		else {
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				if (on) {
					nudgeNotes(0);
				}
				else {
doCancelPopup:
					display->cancelPopup();
				}
			}
			else if (isUIModeActive(UI_MODE_AUDITIONING)) {
				if (!on) {
					timeHorizontalKnobLastReleased = AudioEngine::audioSampleTimer;
					goto doCancelPopup;
				}
			}
			goto passToOthers; // For exiting the UI mode, I think
		}
	}

	// Vertical encoder button
	else if (b == Y_ENC) {

		// If holding notes down...
		if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			if (on) {
				// Just pop up number - don't do anything
				// Wait, why?
				editNoteRepeat(0);
				goto passToOthers;
			}
			else {
				goto doCancelPopup;
			}
		}

		// Or if auditioning...
		if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			if (on) {

				// If in a Kit and multiple Drums auditioned, re-order them
				if (getCurrentOutputType() == OutputType::KIT) {
					for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
						if (yDisplay != lastAuditionedYDisplay && auditionPadIsPressed[yDisplay]) {
							if (inCardRoutine) {
								return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
							}

							actionLogger.deleteAllLogs();
							cancelAllAuditioning();
							InstrumentClip* clip = getCurrentInstrumentClip();
							clip->noteRows.repositionElement(yDisplay + clip->yScroll,
							                                 lastAuditionedYDisplay + clip->yScroll);
							recalculateColours();
							uiNeedsRendering(this);
							goto passToOthers;
						}
					}
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    ((InstrumentClip*)modelStack->getTimelineCounter())
				        ->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack);

				// Just pop up number - don't do anything
				// Wait, why?
				editNumEuclideanEvents(modelStackWithNoteRow, 0, lastAuditionedYDisplay);
				goto passToOthers;
			}
			else {
				goto doCancelPopup;
			}
		}
	}

	else {
passToOthers:
		ActionResult result = InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}

		return ClipView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

void InstrumentClipView::createDrumForAuditionedNoteRow(DrumType drumType) {
	if (getCurrentOutputType() != OutputType::KIT) {
		return;
	}

	if (playbackHandler.isEitherClockActive() && playbackHandler.ticksLeftInCountIn) {
		return;
	}

	int32_t error;
	NoteRow* noteRow;
	int32_t noteRowIndex;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	actionLogger.deleteAllLogs();

	if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		currentUIMode = UI_MODE_AUDITIONING;

		// Make a new NoteRow
		noteRow = createNewNoteRowForKit(modelStack, yDisplayOfNewNoteRow, &noteRowIndex);
		if (!noteRow) {
ramError:
			error = ERROR_INSUFFICIENT_RAM;
someError:
			display->displayError(ERROR_INSUFFICIENT_RAM);
			return;
		}

		uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);

		lastAuditionedYDisplay = yDisplayOfNewNoteRow;
	}

	else {
		cutAuditionedNotesToOne();
		noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(lastAuditionedYDisplay, currentSong, &noteRowIndex);
		if (noteRow->drum) {
			if (drumType != DrumType::SOUND && noteRow->drum->type == drumType) {
				// If it's already that kind of Drum, well, no need to do it again
				return;
			}
			noteRow->drum->drumWontBeRenderedForAWhile();
		}

		auditionPadIsPressed[lastAuditionedYDisplay] = false;
		reassessAuditionStatus(lastAuditionedYDisplay);
	}

	Kit* kit = getCurrentKit();
	if (drumType == DrumType::SOUND) {
		Browser::outputTypeToLoad = OutputType::SYNTH;
		loadInstrumentPresetUI.loadingSynthToKitRow = true;
		loadInstrumentPresetUI.instrumentToReplace = nullptr;

		loadInstrumentPresetUI.instrumentClipToLoadFor = nullptr;
		if (noteRow->drum->type == drumType) {
			loadInstrumentPresetUI.soundDrumToReplace = (SoundDrum*)noteRow->drum;
		}
		else {
			loadInstrumentPresetUI.soundDrumToReplace = nullptr;
		}

		loadInstrumentPresetUI.kitToLoadFor = kit;
		loadInstrumentPresetUI.noteRow = noteRow;
		loadInstrumentPresetUI.noteRowIndex = noteRowIndex;
		openUI(&loadInstrumentPresetUI);
	}

	else {

		Drum* newDrum = storageManager.createNewDrum(drumType);

		if (!newDrum) {
			goto ramError;
		}

		ParamManager paramManager;
		//add sound loading code here

		kit->addDrum(newDrum);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);

		noteRow->setDrum(newDrum, kit, modelStackWithNoteRow, NULL, &paramManager);

		kit->beenEdited();
		drawDrumName(newDrum);
		setSelectedDrum(newDrum, true);
	}

	auditionPadIsPressed[lastAuditionedYDisplay] = true;
	reassessAuditionStatus(lastAuditionedYDisplay);

	// uiNeedsRendering(this, 0, 1 << lastAuditionedNoteOnScreen);
}

void InstrumentClipView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// If they want to copy or paste automation...
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (on && getCurrentOutputType() != OutputType::CV) {
			if (Buttons::isShiftButtonPressed()) {
				pasteAutomation(whichModEncoder);
			}
			else {
				copyAutomation(whichModEncoder);
			}
		}
	}
	else {
		view.modEncoderButtonAction(whichModEncoder, on);
	}
}

void InstrumentClipView::copyAutomation(int32_t whichModEncoder) {
	if (copiedParamAutomation.nodes) {
		delugeDealloc(copiedParamAutomation.nodes);
		copiedParamAutomation.nodes = NULL;
		copiedParamAutomation.numNodes = 0;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);
	if (startPos == endPos) {
		return;
	}

	if (!view.activeModControllableModelStack.modControllable) {
		return;
	}

	ModelStackWithAutoParam* modelStack = view.activeModControllableModelStack.modControllable->getParamFromModEncoder(
	    whichModEncoder, &view.activeModControllableModelStack, false);
	if (modelStack && modelStack->autoParam) {

		// Ok this is cursed, but will work fine so long as
		// the possibly invalid memory here doesn't accidentally
		// equal modelStack->paramCollection.
		bool isPatchCable = (modelStack->paramCollection == modelStack->paramManager->getPatchCableSetAllowJibberish());
		modelStack->autoParam->copy(startPos, endPos, &copiedParamAutomation, isPatchCable, modelStack);

		if (copiedParamAutomation.nodes) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_COPIED));
			return;
		}
	}

	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_AUTOMATION_TO_COPY));
}

void InstrumentClipView::copyNotes() {

	// Clear out previously copied stuff
	deleteCopiedNoteRows();

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);

	copiedScreenWidth = endPos - startPos;
	if (copiedScreenWidth == 0) {
		return;
	}

	copiedScaleType = getCurrentInstrumentClip()->getScaleType();
	//getCurrentClip()->yScroll;
	copiedYNoteOfBottomRow = getCurrentInstrumentClip()->getYNoteFromYDisplay(0, currentSong);

	CopiedNoteRow** prevPointer = &firstCopiedNoteRow;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	uint8_t isFilteredCopy = getNumNoteRowsAuditioning() > 0; // any note rows pesseed

	for (int32_t i = 0; i < getCurrentInstrumentClip()->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = getCurrentInstrumentClip()->noteRows.getElement(i);
		/* this is a little hacky, ideally we could get the yDisplay
		   of thisNoteRow efficiently, but the one we calculate will have to do now

		   considered isNoteRowAuditioning but that required a modelstack and this was leaner
		*/
		int32_t noteRowYDisplay;
		if (getCurrentOutputType() == OutputType::KIT) { // yDisplay for Kits
			noteRowYDisplay = i - getCurrentInstrumentClip()->yScroll;
		}
		else { // Or for non-Kits
			int32_t yVisual = currentSong->getYVisualFromYNote(thisNoteRow->y, getCurrentInstrumentClip()->inScaleMode);
			noteRowYDisplay = yVisual - getCurrentInstrumentClip()->yScroll;
		}
		if (isFilteredCopy) {
			if (!auditionPadIsPressed[noteRowYDisplay])
				continue;
		}

		// If this NoteRow has any notes...
		if (!thisNoteRow->hasNoNotes()) {

			// And if any of them are in the right zone...
			int32_t startI = thisNoteRow->notes.search(startPos, GREATER_OR_EQUAL);
			int32_t endI = thisNoteRow->notes.search(endPos, GREATER_OR_EQUAL);

			int32_t numNotes = endI - startI;

			if (numNotes > 0) {
				// Paul: Might make sense to put these into Internal?
				void* copiedNoteRowMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(CopiedNoteRow));
				if (!copiedNoteRowMemory) {
ramError:
					deleteCopiedNoteRows();
					display->displayError(ERROR_INSUFFICIENT_RAM);
					return;
				}

				// Make the new CopiedNoteRow object
				CopiedNoteRow* newCopiedNoteRow = new (copiedNoteRowMemory) CopiedNoteRow();

				// Put that on the list
				*prevPointer = newCopiedNoteRow;
				prevPointer = &newCopiedNoteRow->next;

				// Allocate some memory for the notes
				// Paul: Might make sense to put these into Internal?
				newCopiedNoteRow->notes = (Note*)GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Note) * numNotes);

				if (!newCopiedNoteRow->notes) {
					goto ramError;
				}

				// Fill in some details for the row
				newCopiedNoteRow->numNotes = numNotes;
				newCopiedNoteRow->yNote = thisNoteRow->y;
				newCopiedNoteRow->yDisplay = noteRowYDisplay;

				// Fill in all the Notes' details
				for (int32_t n = 0; n < numNotes; n++) {
					Note* noteToCopy = thisNoteRow->notes.getElement(n + startI);
					Note* newNote = &newCopiedNoteRow->notes[n];
					newNote->pos = noteToCopy->pos - startPos;
					newNote->length = std::min(
					    noteToCopy->length,
					    endPos
					        - noteToCopy
					              ->pos); // Ensure we don't copy the portion of the tail that extends beyond the screen
					newNote->velocity = noteToCopy->velocity;
					newNote->probability = noteToCopy->probability;
					newNote->lift = noteToCopy->lift;
				}
			}
		}
	}

	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTES_COPIED));
}

void InstrumentClipView::deleteCopiedNoteRows() {
	while (firstCopiedNoteRow) {
		CopiedNoteRow* toDelete = firstCopiedNoteRow;
		firstCopiedNoteRow = firstCopiedNoteRow->next;
		toDelete->~CopiedNoteRow();
		delugeDealloc(toDelete);
	}
}

void InstrumentClipView::pasteAutomation(int32_t whichModEncoder) {
	if (!copiedParamAutomation.nodes) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_AUTOMATION_TO_PASTE));
		return;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);

	int32_t pastedAutomationWidth = endPos - startPos;
	if (pastedAutomationWidth == 0) {
		return;
	}

	float scaleFactor = (float)pastedAutomationWidth / copiedParamAutomation.width;

	if (!view.activeModControllableModelStack.modControllable) {
		return;
	}

	ModelStackWithAutoParam* modelStackWithAutoParam =
	    view.activeModControllableModelStack.modControllable->getParamFromModEncoder(
	        whichModEncoder, &view.activeModControllableModelStack, true);
	if (!modelStackWithAutoParam || !modelStackWithAutoParam->autoParam) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_PASTE_AUTOMATION));
		return;
	}

	Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_PASTE, false);

	if (action) {
		action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithAutoParam, false);
	}

	bool isPatchCable = (modelStackWithAutoParam->paramCollection
	                     == modelStackWithAutoParam->paramManager->getPatchCableSetAllowJibberish());
	// Ok this is cursed, but will work fine so long as
	// the possibly invalid memory here doesn't accidentally
	// equal modelStack->paramCollection.

	modelStackWithAutoParam->autoParam->paste(startPos, endPos, scaleFactor, modelStackWithAutoParam,
	                                          &copiedParamAutomation, isPatchCable);

	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_PASTED));
	if (playbackHandler.isEitherClockActive()) {
		currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
	}
}

void InstrumentClipView::pasteNotes(bool overwriteExisting = true) {

	if (!firstCopiedNoteRow) {
		return;
	}

	if (false) {
ramError:
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	int32_t startPos = getPosFromSquare(0);
	int32_t endPos = getPosFromSquare(kDisplayWidth);

	int32_t pastedScreenWidth = endPos - startPos;
	if (pastedScreenWidth == 0) {
		return;
	}

	ScaleType pastedScaleType = getCurrentInstrumentClip()->getScaleType();

	float scaleFactor = (float)pastedScreenWidth / (uint32_t)copiedScreenWidth;

	Action* action = actionLogger.getNewAction(ACTION_NOTES_PASTE, false);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	if (overwriteExisting) {
		getCurrentInstrumentClip()->clearArea(modelStack, startPos, endPos, action);
	}

	// Kit
	if (getCurrentOutputType() == OutputType::KIT) {
		for (CopiedNoteRow* thisCopiedNoteRow = firstCopiedNoteRow; thisCopiedNoteRow;
		     thisCopiedNoteRow = thisCopiedNoteRow->next) {
			// the vertical offset of the copied y note added to the current yscr
			int32_t noteRowId = thisCopiedNoteRow->yDisplay + getCurrentInstrumentClip()->yScroll;

			if (noteRowId < 0) {
				continue;
			}
			if (noteRowId >= getCurrentInstrumentClip()->noteRows.getNumElements()) {
				break;
			}

			NoteRow* thisNoteRow = getCurrentInstrumentClip()->noteRows.getElement(noteRowId);

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

			bool success = thisNoteRow->paste(modelStackWithNoteRow, thisCopiedNoteRow, scaleFactor, endPos, action);
			if (!success) {
				goto ramError;
			}
		}
	}

	// Non-kit
	else {

		// If neither the source nor the destination was a kit Clip, and one had a scale and the other didn't, we want to preserve some scale information which we otherwise wouldn't
		bool shouldPreserveScale = (copiedScaleType != ScaleType::KIT && copiedScaleType != pastedScaleType);

		for (CopiedNoteRow* thisCopiedNoteRow = firstCopiedNoteRow; thisCopiedNoteRow;
		     thisCopiedNoteRow = thisCopiedNoteRow->next) {
			int32_t yNote;

			if (shouldPreserveScale) {
				yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(0, currentSong) + thisCopiedNoteRow->yNote
				        - copiedYNoteOfBottomRow;
			}
			else {
				yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(thisCopiedNoteRow->yDisplay, currentSong);
			}

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getCurrentInstrumentClip()->getOrCreateNoteRowForYNote(yNote, modelStack, action, NULL);
			NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			if (!thisNoteRow) {
				goto ramError;
			}

			bool success = thisNoteRow->paste(modelStackWithNoteRow, thisCopiedNoteRow, scaleFactor, endPos, action);
			if (!success) {
				goto ramError;
			}
		}
	}

getOut:
	recalculateColours();
	uiNeedsRendering(this);
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTES_PASTED));
}

void InstrumentClipView::doubleClipLengthAction() {

	// If too big...
	if (getCurrentClip()->loopLength > (kMaxSequenceLength >> 1)) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MAXIMUM_LENGTH_REACHED));
		return;
	}

	Action* action = actionLogger.getNewAction(ACTION_CLIP_MULTIPLY, false);

	// Add the ConsequenceClipMultiply to the Action. This must happen before calling doubleClipLength(), which may add note changes and deletions,
	// because when redoing, those have to happen after (and they'll have no effect at all, but who cares)
	if (action) {
		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceInstrumentClipMultiply));

		if (consMemory) {
			ConsequenceInstrumentClipMultiply* newConsequence = new (consMemory) ConsequenceInstrumentClipMultiply();
			action->addConsequence(newConsequence);
		}
	}

	// Double the length, and duplicate the Clip content too
	currentSong->doubleClipLength(getCurrentInstrumentClip(), action);

	zoomToMax(false);

	if (action) {
		action->xZoomClip[AFTER] = currentSong->xZoom[NAVIGATION_CLIP];
		action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
	}

	displayZoomLevel();

	if (display->haveOLED()) {
		display->consoleText("Clip multiplied");
	}
}

void InstrumentClipView::createNewInstrument(OutputType newOutputType) {

	InstrumentClipMinder::createNewInstrument(newOutputType);

	recalculateColours();
	uiNeedsRendering(this);

	if (newOutputType == OutputType::KIT) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		NoteRow* noteRow = getCurrentInstrumentClip()->noteRows.getElement(0);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, noteRow);

		enterDrumCreator(modelStackWithNoteRow);
	}
}

void InstrumentClipView::changeOutputType(OutputType newOutputType) {

	if (getCurrentOutputType() == newOutputType) {
		return;
	}

	InstrumentClipMinder::changeOutputType(newOutputType);

	recalculateColours();
	uiNeedsRendering(this);
}

void InstrumentClipView::selectEncoderAction(int8_t offset) {

	// User may be trying to edit noteCode...
	if (currentUIMode == UI_MODE_AUDITIONING) {
		if (Buttons::isButtonPressed(deluge::hid::button::SELECT_ENC)) {

			if (playbackHandler.isEitherClockActive() && playbackHandler.ticksLeftInCountIn) {
				return;
			}

			cutAuditionedNotesToOne();
			offsetNoteCodeAction(offset);
		}
		else {
			setRowProbability(offset);
		}
	}

	// Or set / create a new Drum
	else if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
		if (Buttons::isButtonPressed(deluge::hid::button::SELECT_ENC)) {
			drumForNewNoteRow = flipThroughAvailableDrums(offset, drumForNewNoteRow, true);
			//setSelectedDrum(drumForNewNoteRow); // Can't - it doesn't have a NoteRow, and so we don't really know where its ParamManager is!
			drawDrumName(drumForNewNoteRow);
		}
	}

	// Or, if user holding a note(s) down, we'll adjust proability instead
	else if (currentUIMode == UI_MODE_NOTES_PRESSED) {
		adjustProbability(offset);
	}
	// Or, normal option - trying to change Instrument presets
	else {
		InstrumentClipMinder::selectEncoderAction(offset);
	}
}

const uint32_t editPadActionUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

const uint32_t mutePadActionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_STUTTERING, 0};

const uint32_t auditionPadActionUIModes[] = {UI_MODE_AUDITIONING,
                                             UI_MODE_ADDING_DRUM_NOTEROW,
                                             UI_MODE_HORIZONTAL_SCROLL,
                                             UI_MODE_RECORD_COUNT_IN,
                                             UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             0};

ActionResult InstrumentClipView::padAction(int32_t x, int32_t y, int32_t velocity) {

	// Drum Randomizer
	if (x == 15 && y == 2 && velocity > 0
	    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer) == RuntimeFeatureStateToggle::On
	    && getCurrentOutputType() == OutputType::KIT
	    && (isUIModeActive(UI_MODE_AUDITIONING) || Buttons::isShiftButtonPressed())) {

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		char chosenFilename[256] = "Nothing to randomize"; // not using "String" to avoid malloc etc. in hot loop

		// Randomize rows with pressed audition pads, or all non-muted rows?
		bool randomizeAll = false;
		int32_t nRows = 8;
		int32_t rowsRandomized = 0;
		if (Buttons::isShiftButtonPressed()) {
			nRows = getCurrentInstrumentClip()->noteRows.getNumElements();
			randomizeAll = true;
		}

		for (int32_t i = 0; i < nRows; i++) {

			// SHOULD this row be randomized?
			if (randomizeAll || auditionPadIsPressed[i]) {
				NoteRow* thisNoteRow;
				if (randomizeAll) {
					thisNoteRow = getCurrentInstrumentClip()->noteRows.getElement(i);
					if (thisNoteRow->muted || thisNoteRow->hasNoNotes()) {
						continue;
					}
				}
				else {
					thisNoteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(i, currentSong);
				}

				// CAN this row be randomized?
				if (thisNoteRow == NULL) {
					continue;
				}
				Drum* drum = thisNoteRow->drum;
				if (!drum || drum->type != DrumType::SOUND) {
					continue;
				}
				SoundDrum* soundDrum = (SoundDrum*)drum;
				MultiRange* r = soundDrum->sources[0].getRange(0);
				if (r == NULL) {
					continue;
				}
				AudioFileHolder* afh = r->getAudioFileHolder();
				if (afh == NULL) {
					continue;
				}
				char const* path = afh->filePath.get();
				if (path == &nothing) {
					continue;
				}
				char* slashAddress = strrchr(path, '/');
				if (slashAddress == NULL) {
					continue;
				}

				// Open directory of current audio file
				*slashAddress = 0;
				FRESULT result = f_opendir(&staticDIR, path);
				*slashAddress = '/';
				if (result != FR_OK) {

					display->displayError(ERROR_SD_CARD);
					return ActionResult::DEALT_WITH;
				}

				// Select random audio file from directory
				int32_t fileCount = 0;
				while (f_readdir(&staticDIR, &staticFNO) == FR_OK && staticFNO.fname[0] != 0) {
					audioFileManager.loadAnyEnqueuedClusters();
					if (staticFNO.fattrib & AM_DIR || !isAudioFilename(staticFNO.fname)) {
						continue;
					}
					if (random(fileCount++) == 0) { // Algorithm: Reservoir Sampling with k=1
						strncpy(chosenFilename, staticFNO.fname, sizeof(chosenFilename));
					}
				}

				// Assign new audio file
				if (fileCount) {
					AudioEngine::stopAnyPreviewing();
					soundDrum->unassignAllVoices();

					afh->setAudioFile(NULL);
					// set the slash to 0 again
					*slashAddress = 0;
					afh->filePath.set(path);

					afh->filePath.concatenate("/");
					afh->filePath.concatenate(chosenFilename);
					afh->loadFile(false, true, true, 1, 0, false);

					soundDrum->name.set(chosenFilename);
					getCurrentInstrument()->beenEdited();
					*slashAddress = '/';
					rowsRandomized++;
				}
			}
		}

		switch (rowsRandomized) {
		case 0:
			break; // if no row was selected and shift was not pressed, we assume it was a regular edit pad press

		case 1:
			display->displayPopup(chosenFilename);
			return ActionResult::DEALT_WITH;
			break;

		default:
			if (randomizeAll) {
				display->displayPopup("Randomized active rows");
			}
			else {
				display->displayPopup("Randomized selected rows");
			}
			return ActionResult::DEALT_WITH;
		}
	}

	// Edit pad action...
	if (x < kDisplayWidth) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Perhaps the user wants to enter the SoundEditor via a shortcut. They can do this by holding an audition pad too - but this gets deactivated
		// if they've done any "euclidean" or per-NoteRow editing already by holding down that audition pad, because if they've done that, they're probably
		// not intending to deliberately go into the SoundEditor, but might be trying to edit notes. Which they currently can't do...
		if (velocity && (!isUIModeActive(UI_MODE_AUDITIONING) || !editedAnyPerNoteRowStuffSinceAuditioningBegan)) {

			ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, velocity);

			if (soundEditorResult == ActionResult::NOT_DEALT_WITH) {
				goto doRegularEditPadActionProbably;
			}
			else {
				return soundEditorResult;
			}
		}

		// Regular edit-pad action
		else {
doRegularEditPadActionProbably:
			if (isUIModeWithinRange(editPadActionUIModes)) {
				editPadAction(velocity, y, x, currentSong->xZoom[NAVIGATION_CLIP]);
			}
		}
	}

	// If mute pad action
	else if (x == kDisplayWidth) {
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (getCurrentOutputType() != OutputType::KIT) {
				return ActionResult::DEALT_WITH;
			}
			NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(y, currentSong);
			if (!noteRow || !noteRow->drum) {
				return ActionResult::DEALT_WITH;
			}
			view.noteRowMuteMidiLearnPadPressed(velocity, noteRow);
		}
		else if (getCurrentOutputType() == OutputType::KIT && lastAuditionedYDisplay == y
		         && isUIModeActive(UI_MODE_AUDITIONING) && getNumNoteRowsAuditioning() == 1) {
			if (velocity) {
				if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
					enterUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				}
				else {
					goto maybeRegularMutePress;
				}
			}
			else {
				if (isUIModeActive(UI_MODE_DRAGGING_KIT_NOTEROW)) {
					exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				}
				else {
					goto maybeRegularMutePress;
				}
			}
		}
		else {
maybeRegularMutePress:
			if (isUIModeWithinRange(mutePadActionUIModes) && velocity) {
				mutePadPress(y);
			}
		}
	}

	// Audition pad action
	else {
possiblyAuditionPad:
		if (x == kDisplayWidth + 1) {

			// "Learning" to this audition pad:
			if (isUIModeActiveExclusively(UI_MODE_MIDI_LEARN)) {
				if (getCurrentUI() == this) {
					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					if (getCurrentOutputType() == OutputType::KIT) {
						NoteRow* thisNoteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(y, currentSong);
						if (!thisNoteRow || !thisNoteRow->drum) {
							return ActionResult::DEALT_WITH;
						}
						view.drumMidiLearnPadPressed(velocity, thisNoteRow->drum, getCurrentKit());
					}
					else {
						view.melodicInstrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)getCurrentOutput());
					}
				}
			}

			// Changing the scale:
			else if (isUIModeActiveExclusively(UI_MODE_SCALE_MODE_BUTTON_PRESSED)) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				if (velocity
				    && getCurrentOutputType()
				           != OutputType::
				               KIT) { // We probably couldn't have got this far if it was a Kit, but let's just check
					if (getCurrentInstrumentClip()->inScaleMode) {
						currentUIMode = UI_MODE_NONE; // So that the upcoming render of the sidebar comes out correctly
						changeRootNote(y);
						exitScaleModeOnButtonRelease = false;
					}
					else {
						enterScaleMode(y);
					}
				}
			}

			// Actual basic audition pad press:
			else if (!velocity || isUIModeWithinRange(auditionPadActionUIModes)) {
				exitUIMode(UI_MODE_DRAGGING_KIT_NOTEROW);
				if (sdRoutineLock && !allowSomeUserActionsEvenWhenInCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allowable sometimes if in card routine.
				}
				auditionPadAction(velocity, y, Buttons::isShiftButtonPressed());
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

uint8_t InstrumentClipView::getEditPadPressXDisplayOnScreen(uint8_t yDisplay) {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) {
			return editPadPresses[i].xDisplay;
		}
	}

	return 0; // Presumably impossible case
}

void InstrumentClipView::editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom) {

	uint32_t squareStart = getPosFromSquare(xDisplay);

	InstrumentClip* clip = getCurrentInstrumentClip();
	Instrument* instrument = (Instrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If button down
	if (state) {

		// Don't allow further new presses if already done nudging
		if (numEditPadPresses && doneAnyNudgingSinceFirstEditPadPress) {
			return;
		}

		if (!isSquareDefined(xDisplay)) {
			return;
		}

		// Get existing NoteRow if there was one
		ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

		// If no NoteRow yet...
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

			// Just check we're not beyond Clip length
			if (squareStart >= clip->loopLength) {
				return;
			}

			// And create the new NoteRow
			modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				if (instrument->type == OutputType::KIT) {
					setSelectedDrum(NULL);
				}
				return;
			}

			// If that just created a new NoteRow for a Kit, then we can't undo any further back than this
			if (instrument->type == OutputType::KIT) {
				actionLogger.deleteAllLogs();
			}
		}

		int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();

		// Now that we've definitely got a NoteRow, check against NoteRow "effective" length here (though it'll very possibly be the same as the Clip length we may have tested against above).
		if (squareStart >= effectiveLength) {
			return;
		}

		uint32_t squareWidth = getSquareWidth(xDisplay, effectiveLength);

		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

		ParamManagerForTimeline* paramManager = NULL;
		if (instrument->type == OutputType::SYNTH) {
			paramManager = &clip->paramManager;
		}
		else if (instrument->type == OutputType::KIT) {
			paramManager = &noteRow->paramManager;
		}

		// If this is a note-length-edit press...
		if (numEditPadPressesPerNoteRowOnScreen[yDisplay] == 1
		    && (int32_t)(timeLastEditPadPress + 80 * 44 - AudioEngine::audioSampleTimer) < 0
		    && clip->allowNoteTails(modelStackWithNoteRow) && getEditPadPressXDisplayOnScreen(yDisplay) < xDisplay) {

			// Find that original press
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) {
					break;
				}
			}

			// If we found it...
			if (i < kEditPadPressBufferSize) {

				int32_t oldLength;
				int32_t noteStartPos;

				// If multiple notes, pick the last one
				if (editPadPresses[i].isBlurredSquare) {
					int32_t noteI = noteRow->notes.search(squareStart + squareWidth, LESS);
					Note* note = noteRow->notes.getElement(noteI);
					if (note) {
						oldLength = note->getLength();
						noteStartPos = note->pos;
					}
				}

				else {
					oldLength = editPadPresses[i].intendedLength;
					noteStartPos = editPadPresses[i].intendedPos;
				}

				// First, figure out the lengh to take the note up to the start of the pressed square. Put it in newLength
				int32_t newLength = squareStart - noteStartPos;
				if (newLength < 0) {
					newLength += effectiveLength; // Wrapped note
				}

				// If current square wasn't occupied at all to begin with, fill it up
				if (oldLength <= newLength) {
					newLength += squareWidth;
				}

				if (newLength == 0) {
					newLength = squareWidth; // Protection - otherwise we could end up with a 0-length note!
				}

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				int32_t areaStart, areaWidth;
				bool actuallyExtendNoteAtStartOfArea = (newLength > oldLength);

				if (actuallyExtendNoteAtStartOfArea) { // Increasing length

					// Make sure it doesn't eat into the next note
					int32_t maxLength = noteRow->getDistanceToNextNote(noteStartPos, modelStackWithNoteRow);
					newLength = std::min(newLength, maxLength);

					areaStart = noteStartPos;
					areaWidth = newLength;
				}

				else { // Decreasing length
					areaStart = noteStartPos + newLength;
					areaWidth = oldLength - newLength;
				}

				noteRow->clearArea(areaStart, areaWidth, modelStackWithNoteRow, action, clip->getWrapEditLevel(),
				                   actuallyExtendNoteAtStartOfArea);

				if (!editPadPresses[i].isBlurredSquare) {
					editPadPresses[i].intendedLength = newLength;
				}
				editPadPresses[i].deleteOnDepress = false;
				uiNeedsRendering(this, 1 << yDisplay, 0);

				if (instrument->type == OutputType::KIT) {
					setSelectedDrum(noteRow->drum);
				}
			}
		}

		// Or, if this is a regular create-or-select press...
		else {

			timeLastEditPadPress = AudioEngine::audioSampleTimer;
			// Find an empty space in the press buffer, if there is one
			int32_t i;
			for (i = 0; i < kEditPadPressBufferSize; i++) {
				if (!editPadPresses[i].isActive) {
					break;
				}
			}
			if (i < kEditPadPressBufferSize) {

				ParamManagerForTimeline* paramManagerDummy;
				Sound* sound = getSoundForNoteRow(noteRow, &paramManagerDummy);

				uint32_t whichRowsToReRender = (1 << yDisplay);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				uint32_t desiredNoteLength = squareWidth;
				if (sound) {

					int32_t yNote;

					if (instrument->type == OutputType::KIT) {
						yNote = 60;
					}
					else {
						yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					}

					// If a time-synced sample...
					uint32_t sampleLength = sound->hasAnyTimeStretchSyncing(paramManager, true, yNote);
					if (sampleLength) {
						uint32_t sampleLengthInTicks =
						    ((uint64_t)sampleLength << 32) / currentSong->timePerTimerTickBig;

						// Previously I was having it always jump to a "square" number, but as James Meharry pointed out, what if the Clip is deliberately a
						// non-square length?
						desiredNoteLength = effectiveLength;
						while (!(desiredNoteLength & 1)) {
							desiredNoteLength >>= 1;
						}

						while (desiredNoteLength * 1.41 < sampleLengthInTicks) {
							desiredNoteLength <<= 1;
						}

						// If desired note length too long and no existing notes, extend the Clip (or if the NoteRow has independent length, do that instead).
						if (noteRow->hasNoNotes() && !clip->wrapEditing && desiredNoteLength > effectiveLength) {
							squareStart = 0;
							if (noteRow->loopLengthIfIndependent) {
								noteRow->loopLengthIfIndependent = desiredNoteLength;
							}
							else {
								currentSong->setClipLength(clip, desiredNoteLength, action);

								// Clip length changing may visually change other rows too, so must re-render them all
								whichRowsToReRender = 0xFFFFFFFF;
							}
						}
					}

					// Or if general cut-mode samples - but only for kit Clips, not synth
					else if (instrument->type == OutputType::KIT) {
						bool anyLooping;
						sampleLength = sound->hasCutOrLoopModeSamples(paramManager, yNote, &anyLooping);
						if (sampleLength) {

							// If sample loops, we want to cut out before we get to the loop-point
							if (anyLooping) {
								desiredNoteLength = ((uint64_t)sampleLength << 32) / currentSong->timePerTimerTickBig;
							}

							// Or if sample doesn't loop, we want to extend just past the end point
							else {
								desiredNoteLength =
								    (int32_t)((sampleLength - 2) / currentSong->getTimePerTimerTickFloat()) + 1;
							}
						}
					}

					desiredNoteLength = std::max(desiredNoteLength, squareWidth);
				}

				uint32_t maxNoteLengthHere = clip->getWrapEditLevel();
				desiredNoteLength = std::min(desiredNoteLength, maxNoteLengthHere);

				Note* firstNote;
				Note* lastNote;
				uint8_t squareType =
				    noteRow->getSquareType(squareStart, squareWidth, &firstNote, &lastNote, modelStackWithNoteRow,
				                           clip->allowNoteTails(modelStackWithNoteRow), desiredNoteLength, action,
				                           playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip),
				                           isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON));

				// If error (no ram left), get out
				if (!squareType) {
					display->displayError(ERROR_INSUFFICIENT_RAM);
					return;
				}

				// Otherwise, we've selected a note
				else {

					shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;

					// If this is the first press, record the time
					if (numEditPadPresses == 0) {
						timeFirstEditPadPress = AudioEngine::audioSampleTimer;
						doneAnyNudgingSinceFirstEditPadPress = false;
						offsettingNudgeNumberDisplay = false;
						shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
					}

					if (squareType == SQUARE_BLURRED) {
						editPadPresses[i].intendedPos = squareStart;
						editPadPresses[i].intendedLength = squareWidth;
						editPadPresses[i].deleteOnDepress = true;
					}
					else {
						editPadPresses[i].intendedPos = lastNote->pos;
						editPadPresses[i].intendedLength = lastNote->getLength();
						editPadPresses[i].deleteOnDepress =
						    (squareType == SQUARE_NOTE_HEAD || squareType == SQUARE_NOTE_TAIL_UNMODIFIED);
					}

					editPadPresses[i].isBlurredSquare = (squareType == SQUARE_BLURRED);
					editPadPresses[i].intendedVelocity = firstNote->getVelocity();
					editPadPresses[i].intendedProbability = firstNote->getProbability();
					editPadPresses[i].isActive = true;
					editPadPresses[i].yDisplay = yDisplay;
					editPadPresses[i].xDisplay = xDisplay;
					editPadPresses[i].deleteOnScroll = true;
					editPadPresses[i].mpeCachedYet = false;
					for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
						editPadPresses[i].stolenMPE[m].num = 0;
					}
					numEditPadPresses++;
					numEditPadPressesPerNoteRowOnScreen[yDisplay]++;
					enterUIMode(UI_MODE_NOTES_PRESSED);

					// If new note...
					if (squareType == SQUARE_NEW_NOTE) {

						// If we're cross-screen-editing, create other corresponding notes too
						if (clip->wrapEditing) {
							int32_t error = noteRow->addCorrespondingNotes(
							    squareStart, desiredNoteLength, editPadPresses[i].intendedVelocity,
							    modelStackWithNoteRow, clip->allowNoteTails(modelStackWithNoteRow), action);

							if (error) {
								display->displayError(ERROR_INSUFFICIENT_RAM);
							}
						}
					}

					// Edit mod knob values for this Note's region
					int32_t distanceToNextNote = clip->getDistanceToNextNote(lastNote, modelStackWithNoteRow);

					if (instrument->type == OutputType::KIT) {
						setSelectedDrum(noteRow->drum);
					}

					// Can only set the mod region after setting the selected drum! Otherwise the params' currentValues don't end up right
					view.setModRegion(
					    firstNote->pos,
					    std::max((uint32_t)distanceToNextNote + lastNote->pos - firstNote->pos, squareWidth),
					    modelStackWithNoteRow->noteRowId);

					// Now that we're holding a note down, get set up for if the user wants to edit its MPE values.
					for (int32_t t = 0; t < MPE_RECORD_LENGTH_FOR_NOTE_EDITING; t++) {
						mpeValuesAtHighestPressure[t][0] = 0;
						mpeValuesAtHighestPressure[t][1] = 0;
						mpeValuesAtHighestPressure[t][2] = -1; // -1 means not valid yet
					}
					mpeMostRecentPressure = 0;
					mpeRecordLastUpdateTime = AudioEngine::audioSampleTimer;

					reassessAuditionStatus(yDisplay);
				}

				// Might need to re-render row, if it was changed
				if (squareType == SQUARE_NEW_NOTE || squareType == SQUARE_NOTE_TAIL_MODIFIED) {
					uiNeedsRendering(this, whichRowsToReRender, 0);
				}
			}
		}
	}

	// Or if pad press ended...
	else {

		// Find the corresponding press, if there is one
		int32_t i;
		for (i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay
			    && editPadPresses[i].xDisplay == xDisplay) {
				break;
			}
		}

		// If we found it...
		if (i < kEditPadPressBufferSize) {

			display->cancelPopup(); // Crude way of getting rid of the probability-editing permanent popup

			uint8_t velocity = editPadPresses[i].intendedVelocity;

			// Must mark it as inactive first, otherwise, the note-deletion code may do so and then we'd do it again here
			endEditPadPress(i);

			// If we're meant to be deleting it on depress...
			if (editPadPresses[i].deleteOnDepress
			    && AudioEngine::audioSampleTimer - timeLastEditPadPress < kShortPressTime) {

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, modelStack);

				Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

				int32_t wrapEditLevel = clip->getWrapEditLevel();

				noteRow->clearArea(squareStart, getSquareWidth(xDisplay, modelStackWithNoteRow->getLoopLength()),
				                   modelStackWithNoteRow, action, wrapEditLevel);

				noteRow->clearMPEUpUntilNextNote(modelStackWithNoteRow, squareStart, wrapEditLevel, true);

				uiNeedsRendering(this, 1 << yDisplay, 0);
			}

			// Or if not deleting...
			else {
				instrument->defaultVelocity = velocity;
			}

			// Close last note nudge action, if there was one - so each such action is for one consistent set of notes
			actionLogger.closeAction(ACTION_NOTE_NUDGE);

			// If *all* presses are now ended
			checkIfAllEditPadPressesEnded();

			reassessAuditionStatus(yDisplay);
		}
	}
}

Sound* InstrumentClipView::getSoundForNoteRow(NoteRow* noteRow, ParamManagerForTimeline** getParamManager) {
	if (getCurrentOutputType() == OutputType::SYNTH) {
		*getParamManager = &getCurrentClip()->paramManager;
		return (SoundInstrument*)getCurrentOutput();
	}
	else if (getCurrentOutputType() == OutputType::KIT && noteRow && noteRow->drum
	         && noteRow->drum->type == DrumType::SOUND) {
		if (!noteRow) {
			return NULL;
		}
		*getParamManager = &noteRow->paramManager;
		return (SoundDrum*)noteRow->drum;
	}
	else {
		*getParamManager = NULL;
		return NULL;
	}
}

void InstrumentClipView::endEditPadPress(uint8_t i) {
	editPadPresses[i].isActive = false;
	numEditPadPresses--;
	numEditPadPressesPerNoteRowOnScreen[editPadPresses[i].yDisplay]--;

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		if (editPadPresses[i].stolenMPE[m].num) {
			delugeDealloc(editPadPresses[i].stolenMPE[m].nodes);
		}
	}
}

void InstrumentClipView::checkIfAllEditPadPressesEnded(bool mayRenderSidebar) {
	if (numEditPadPresses == 0) {
		view.setModRegion();
		exitUIMode(UI_MODE_NOTES_PRESSED);
		actionLogger.closeAction(ACTION_NOTE_EDIT);
		quantizeAmount = 0;
	}
}

void InstrumentClipView::adjustVelocity(int32_t velocityChange) {

	int32_t velocityValue = 0;

	Action* action;
	if (display->haveOLED() || display->hasPopup()) {
		action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		if (!action) {
			return; // Necessary why?
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive) {
			editPadPresses[i].deleteOnDepress = false;

			int32_t noteRowIndex;
			NoteRow* noteRow =
			    getCurrentInstrumentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
			int32_t noteRowId = getCurrentInstrumentClip()->getNoteRowId(noteRow, noteRowIndex);

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

			// Multiple notes in square
			if (editPadPresses[i].isBlurredSquare) {

				uint32_t velocitySumThisSquare = 0;
				uint32_t numNotesThisSquare = 0;

				int32_t noteI = noteRow->notes.search(editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(noteI);
				while (note && note->pos - editPadPresses[i].intendedPos < editPadPresses[i].intendedLength) {
					if (display->hasPopup()) {
						noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
						                                     CORRESPONDING_NOTES_ADJUST_VELOCITY, velocityChange);
					}

					if (velocityValue == 0) {
						velocityValue = note->getVelocity();
					}
					else {
						if (velocityValue != note->getVelocity()) {
							velocityValue = 255; // Means "multiple"
						}
					}
					numNotesThisSquare++;
					velocitySumThisSquare += note->getVelocity();

					noteI++;
					note = noteRow->notes.getElement(noteI);
				}

				editPadPresses[i].intendedVelocity =
				    velocitySumThisSquare
				    / numNotesThisSquare; // Get the average. Ideally we'd have done this when first selecting the note too, but I didn't
			}

			// Only one note in square
			else {
				if (display->hasPopup()) {
					editPadPresses[i].intendedVelocity =
					    std::clamp<int32_t>((int32_t)editPadPresses[i].intendedVelocity + velocityChange, 1, 127);
					noteRow->changeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_ADJUST_VELOCITY, velocityChange);
				}

				if (velocityValue == 0) {
					velocityValue = editPadPresses[i].intendedVelocity;
				}
				else {
					if (velocityValue != editPadPresses[i].intendedVelocity) {
						velocityValue = 255; // Means "multiple"
					}
				}
			}
		}
	}

	if (velocityValue) {
		char buffer[22];
		char const* displayString;
		if (velocityValue == 255) {
			if (velocityChange >= 0) {
				displayString = deluge::l10n::get(deluge::l10n::String::STRING_FOR_VELOCITY_INCREASED);
			}
			else {
				displayString = deluge::l10n::get(deluge::l10n::String::STRING_FOR_VELOCITY_DECREASED);
			}

			// Don't bother trying to think of some smart way to update lastVelocityInteractedWith. It'll get updated when user releases last press.
		}
		else {
			if (display->haveOLED()) {
				strcpy(buffer, "Velocity: ");
				intToString(velocityValue, buffer + strlen(buffer));
			}
			else {

				intToString(velocityValue, buffer);
			}

			displayString = buffer;
			getCurrentInstrument()->defaultVelocity = velocityValue;
		}
		if (display->haveOLED()) {
			display->popupText(displayString);
		}
		else {
			display->displayPopup(displayString, 0, true);
		}
	}

	reassessAllAuditionStatus();
}

void InstrumentClipView::adjustProbability(int32_t offset) {

	int32_t probabilityValue = -1;

	bool prevBase = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If just one press...
	if (numEditPadPresses == 1) {
		// Find it
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				if (editPadPresses[i].isBlurredSquare) {
					goto multiplePresses;
				}

				int32_t probability = editPadPresses[i].intendedProbability;

				probabilityValue = probability & 127;
				prevBase = (probability & 128);

				// If editing, continue edit
				if (display->hasPopup()) {
					Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
					if (!action) {
						return;
					}

					// Incrementing
					if (offset == 1) {
						if (probabilityValue < kNumProbabilityValues + kNumIterationValues) {
							if (prevBase) {
								probabilityValue++;
								prevBase = false;
							}
							else {
								// From FILL (value: 0) we go up to NOT FILL (value: 0 | 128, that is prob=0 + prevBase=true)
								// And for percentage-probabilities we set preBase if there are previous notes with the same probability
								if (probabilityValue == 0
								    || (probabilityValue < kNumProbabilityValues
								        && getCurrentInstrumentClip()->doesProbabilityExist(
								            editPadPresses[i].intendedPos, probabilityValue,
								            kNumProbabilityValues - probabilityValue))) {
									prevBase = true;
								}
								else {
									probabilityValue++;
								}
							}
						}
					}

					// Decrementing
					else {
						if (probabilityValue > 0 || prevBase) {
							if (prevBase) {
								prevBase = false;
							}
							else {
								probabilityValue--;
								// From 5% (value: 1) we go down to NOT FILL (value: 0 | 128, that is prob=0 + prevBase=true)
								// From any other percentage-probability we set prevBase if there are previous notes with the same probability
								prevBase = (probabilityValue == 0
								            || probabilityValue < kNumProbabilityValues
								                   && getCurrentInstrumentClip()->doesProbabilityExist(
								                       editPadPresses[i].intendedPos, probabilityValue,
								                       kNumProbabilityValues - probabilityValue));
							}
						}
					}

					editPadPresses[i].intendedProbability = probabilityValue;
					if (prevBase) {
						editPadPresses[i].intendedProbability |= 128;
					}

					int32_t noteRowIndex;
					NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay,
					                                                                  currentSong, &noteRowIndex);
					int32_t noteRowId = getCurrentInstrumentClip()->getNoteRowId(noteRow, noteRowIndex);
					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

					noteRow->changeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_PROBABILITY,
					                                     editPadPresses[i].intendedProbability);
				}
				break;
			}
		}
	}

	// Or if multiple presses...
	else {
multiplePresses:

		int32_t leftMostPos = 2147483647;
		int32_t leftMostIndex;
		// Find the leftmost one. There may be more than one...
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				// "blurred square" with multiple notes
				if (editPadPresses[i].isBlurredSquare) {
					NoteRow* noteRow =
					    getCurrentInstrumentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong);
					int32_t noteI = noteRow->notes.search(editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
					Note* note = noteRow->notes.getElement(noteI);
					if (note) {
						editPadPresses[i].intendedProbability =
						    note->probability; // This might not have been grabbed properly initially
						if (note->pos < leftMostPos) {
							leftMostPos = note->pos;
							leftMostIndex = i;
						}
					}
				}

				// Or, just 1 note in square
				else {

					if (editPadPresses[i].intendedPos < leftMostPos) {
						leftMostPos = editPadPresses[i].intendedPos;
						leftMostIndex = i;
					}
				}
			}
		}

		// Decide the probability, based on the existing probability of the leftmost note
		uint8_t probability = editPadPresses[leftMostIndex].intendedProbability;
		probabilityValue = probability & 127;
		prevBase = (probability & 128);

		// If editing, continue edit
		if (display->hasPopupOfType(DisplayPopupType::PROBABILITY)) {
			Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
			if (!action) {
				return;
			}

			// Incrementing
			if (offset == 1) {
				if (probabilityValue == 0) {
					// From NOT FILL (value: 0 | 128) we go up to 5% (value: 1)
					if (prevBase) {
						probabilityValue = 1;
						prevBase = false;
					}
					// From FILL (value: 0) we go up to NOT FILL (value: 0 | 128)
					else {
						prevBase = true;
					}
				}
				// In any other case we just increment probability value
				else if (probabilityValue < kNumProbabilityValues + kNumIterationValues) {
					probabilityValue++;
					// As we are treating multiple notes, we need to reset prevBase and remove the "latching" state for leftMostNote
					prevBase = false;
				}
			}
			// Decrementing
			else {
				if (probabilityValue == 1) {
					// From 5% (value: 1) we go down to NOT FILL (value: 0 | 128)
					prevBase = true;
					probabilityValue = 0;
				}
				else if (probabilityValue == 0 && prevBase) {
					// From NOT FILL (value: 0 | 128) we go down to FILL (value: 0)
					prevBase = false;
				}
				// In any other case we just increment probability value
				else if (probabilityValue > 1) {
					probabilityValue--;
					// As we are treating multiple notes, we need to reset prevBase and remove the "latching" state for leftMostNote
					prevBase = false;
				}
			}

			uint8_t probabilityForMultipleNotes = probabilityValue;
			if (prevBase) {
				probabilityForMultipleNotes |= 128;
			}

			// Set the probability of the other presses, and update all probabilities with the actual notes
			for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive) {

					// Update probability
					editPadPresses[i].intendedProbability = probabilityForMultipleNotes;

					int32_t noteRowIndex;
					NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay,
					                                                                  currentSong, &noteRowIndex);
					int32_t noteRowId = getCurrentInstrumentClip()->getNoteRowId(noteRow, noteRowIndex);

					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

					// "blurred square" with multiple notes
					if (editPadPresses[i].isBlurredSquare) {

						int32_t noteI = noteRow->notes.search(editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
						Note* note = noteRow->notes.getElement(noteI);
						while (note && note->pos - editPadPresses[i].intendedPos < editPadPresses[i].intendedLength) {

							// And if not one of the leftmost notes, make it a prev-base one - if we're doing actual percentage probabilities
							if (probabilityValue > 0 && probabilityValue < kNumProbabilityValues
							    && note->pos != leftMostPos) {
								editPadPresses[i].intendedProbability |= 128;
							}
							noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
							                                     CORRESPONDING_NOTES_SET_PROBABILITY,
							                                     editPadPresses[i].intendedProbability);

							noteI++;
							note = noteRow->notes.getElement(noteI);
						}
					}
					// Or, just 1 note in square
					else {
						// And if not one of the leftmost notes, make it a prev-base one - if we're doing actual percentage probabilities
						if (probabilityValue > 0 && probabilityValue < kNumProbabilityValues
						    && editPadPresses[i].intendedPos != leftMostPos) {
							editPadPresses[i].intendedProbability |= 128;
						}
						noteRow->changeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow,
						                                     action, CORRESPONDING_NOTES_SET_PROBABILITY,
						                                     editPadPresses[i].intendedProbability);
					}
				}
			}
		}
	}

	if (probabilityValue != -1) {
		displayProbability(probabilityValue, prevBase);
	}
}

void InstrumentClipView::mutePadPress(uint8_t yDisplay) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	// We do not want to change the selected Drum if stutter is happening, because the user needs to keep controlling, and eventually stop stuttering on, their current selected Drum
	bool wasStuttering = isUIModeActive(UI_MODE_STUTTERING);

	// Try getting existing NoteRow.
	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

	// If no existing NoteRow...
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

		// For Kits, get out.
		if (clip->output->type == OutputType::KIT) {
fail:
			if (!wasStuttering) {
				setSelectedDrum(NULL);
			}
			return;
		}

		// Create new NoteRow.
		modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
			return;
		}
	}

	NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

	clip->toggleNoteRowMute(modelStackWithNoteRow);

	if (!wasStuttering && clip->output->type == OutputType::KIT) {
		setSelectedDrum(noteRow->drum);
	}

	uiNeedsRendering(this, 0, 1 << yDisplay);
}

NoteRow* InstrumentClipView::createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, int32_t yDisplay,
                                                    int32_t* getIndex) {
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	NoteRow* newNoteRow = clip->createNewNoteRowForKit(modelStack, (yDisplay < -clip->yScroll), getIndex);
	if (!newNoteRow) {
		return NULL; // If memory full
	}

	recalculateColour(yDisplay);

	return newNoteRow;
}

ModelStackWithNoteRow* InstrumentClipView::getOrCreateNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack,
                                                                         int32_t yDisplay) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
	}

	return modelStackWithNoteRow;
}

ModelStackWithNoteRow* InstrumentClipView::createNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack,
                                                                    int32_t yDisplay) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	NoteRow* noteRow = NULL;
	int32_t noteRowId;

	// If *not* a kit
	if (clip->output->type != OutputType::KIT) {

		noteRow = clip->createNewNoteRowForYVisual(getYVisualFromYDisplay(yDisplay), modelStack->song);

		if (!noteRow) { // If memory full
doDisplayError:
			display->displayError(ERROR_INSUFFICIENT_RAM);
		}
		else {
			noteRowId = noteRow->y;
		}
	}

	// Or, if a kit
	else {
		// If it's more than one row below, we can't do it
		if (yDisplay < -1 - clip->yScroll) {
			goto getOut;
		}

		// If it's more than one row above, we can't do it
		if (yDisplay > clip->getNumNoteRows() - clip->yScroll) {
			goto getOut;
		}

		noteRow = createNewNoteRowForKit(modelStack, yDisplay, &noteRowId);

		if (!noteRow) {
			goto doDisplayError;
		}

		else {
			uiNeedsRendering(this, 0, 1 << yDisplay);
		}
	}

getOut:
	return modelStack->addNoteRow(noteRowId, noteRow);
}

void InstrumentClipView::recalculateColours() {
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		recalculateColour(yDisplay);
	}
}

void InstrumentClipView::recalculateColour(uint8_t yDisplay) {
	int32_t colourOffset = 0;
	NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);
	if (noteRow) {
		colourOffset = noteRow->getColourOffset(getCurrentInstrumentClip());
	}
	rowColour[yDisplay] = getCurrentInstrumentClip()->getMainColourFromY(
	    getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong), colourOffset);
	rowTailColour[yDisplay] = rowColour[yDisplay].forTail();
	rowBlurColour[yDisplay] = rowColour[yDisplay].forBlur();

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_RECALCULATE_COLOUR);
}

ActionResult InstrumentClipView::scrollVertical(int32_t scrollAmount, bool inCardRoutine, bool draggingNoteRow) {
	int32_t noteRowToShiftI;
	int32_t noteRowToSwapWithI;

	bool isKit = getCurrentOutputType() == OutputType::KIT;

	// If a Kit...
	if (isKit) {
		// Limit scrolling
		if (scrollAmount >= 0) {
			if ((int16_t)(getCurrentInstrumentClip()->yScroll + scrollAmount)
			    > (int16_t)(getCurrentInstrumentClip()->getNumNoteRows() - 1)) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (getCurrentInstrumentClip()->yScroll + scrollAmount < 1 - kDisplayHeight) {
				return ActionResult::DEALT_WITH;
			}
		}

		// Limit how far we can shift a NoteRow
		if (draggingNoteRow) {
			noteRowToShiftI = lastAuditionedYDisplay + getCurrentInstrumentClip()->yScroll;
			if (noteRowToShiftI < 0 || noteRowToShiftI >= getCurrentInstrumentClip()->noteRows.getNumElements()) {
				return ActionResult::DEALT_WITH;
			}

			if (scrollAmount >= 0) {
				if (noteRowToShiftI >= getCurrentInstrumentClip()->noteRows.getNumElements() - 1) {
					return ActionResult::DEALT_WITH;
				}
				noteRowToSwapWithI = noteRowToShiftI + 1;
			}
			else {
				if (noteRowToShiftI == 0) {
					return ActionResult::DEALT_WITH;
				}
				noteRowToSwapWithI = noteRowToShiftI - 1;
			}
		}
	}

	// Or if not a Kit...
	else {
		int32_t newYNote;
		if (scrollAmount > 0) {
			newYNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(kDisplayHeight - 1 + scrollAmount, currentSong);
		}
		else {
			newYNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(scrollAmount, currentSong);
		}

		if (!getCurrentInstrumentClip()->isScrollWithinRange(scrollAmount, newYNote)) {
			return ActionResult::DEALT_WITH;
		}
	}

	if (inCardRoutine && (numEditPadPresses || draggingNoteRow)) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool currentClipIsActive = currentSong->isClipActive(getCurrentClip());

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Switch off any auditioned notes. But leave on the one whose NoteRow we're moving, if we are
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255
		    && (!draggingNoteRow || lastAuditionedYDisplay != yDisplay)) {
			sendAuditionNote(false, yDisplay, 127, 0);

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, modelStack);
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				// If recording, record a note-off for this NoteRow, if one exists
				if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {
					getCurrentInstrumentClip()->recordNoteOff(modelStackWithNoteRow);
				}
			}
		}
	}

	// If any presses happening, grab those Notes...
	if (numEditPadPresses) {

		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);

		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				if (editPadPresses[i].isBlurredSquare) {
					endEditPadPress(i); // We can't deal with multiple notes per square
					checkIfAllEditPadPressesEnded(false);
					reassessAuditionStatus(editPadPresses[i].yDisplay);
				}
				else {
					if (editPadPresses[i].deleteOnScroll) {
						int32_t pos = editPadPresses[i].intendedPos;
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    getCurrentInstrumentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);
						NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRow();
						thisNoteRow->deleteNoteByPos(modelStackWithNoteRow, pos, action);

						ParamCollectionSummary* mpeParamsSummary =
						    thisNoteRow->paramManager.getExpressionParamSetSummary();
						ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
						if (mpeParams) {
							int32_t distanceToNextNote = thisNoteRow->getDistanceToNextNote(pos, modelStackWithNoteRow);
							int32_t loopLength = modelStackWithNoteRow->getLoopLength();
							ModelStackWithParamCollection* modelStackWithParamCollection =
							    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
							        mpeParams, mpeParamsSummary);

							for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
								StolenParamNodes* stolenNodeRecord = NULL;
								if (!editPadPresses[i].mpeCachedYet) {
									stolenNodeRecord = &editPadPresses[i].stolenMPE[m];
								}
								AutoParam* param = &mpeParams->params[m];
								ModelStackWithAutoParam* modelStackWithAutoParam =
								    modelStackWithParamCollection->addAutoParam(m, param);

								param->stealNodes(modelStackWithAutoParam, pos, distanceToNextNote, loopLength, action,
								                  stolenNodeRecord);
							}
						}

						editPadPresses[i].mpeCachedYet = true;
					}
				}
			}
		}
	}

	// Shift the selected NoteRow, if that's what we're doing. We know we're in Kit mode then
	if (draggingNoteRow) {

		actionLogger.deleteAllLogs(); // Can't undo past this!

		getCurrentInstrumentClip()->noteRows.getElement(noteRowToShiftI)->y =
		    -32768; // Need to remember not to try and use the yNote value of this NoteRow if we switch back out of Kit mode
		getCurrentInstrumentClip()->noteRows.swapElements(noteRowToShiftI, noteRowToSwapWithI);
	}

	// Do actual scroll
	getCurrentInstrumentClip()->yScroll += scrollAmount;

	recalculateColours(); // Don't render - we'll do that after we've dealt with presses (potentially creating Notes)

	// Switch on any auditioned notes - remembering that the one we're shifting (if we are) was left on before
	bool drawnNoteCodeYet = false;
	bool forceStoppedAnyAuditioning = false;
	bool changedActiveModControllable = false;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			// If shifting a NoteRow..
			if (draggingNoteRow && lastAuditionedYDisplay == yDisplay) {}

			// Otherwise, switch its audition back on
			else {
				// Check NoteRow exists, incase we've got a Kit
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, modelStack);

				if (!isKit || modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (modelStackWithNoteRow->getNoteRowAllowNull()
					    && modelStackWithNoteRow->getNoteRow()->soundingStatus == STATUS_SEQUENCED_NOTE) {}
					else {

						// Record note-on if we're recording
						if (playbackHandler.shouldRecordNotesNow() && currentClipIsActive) {

							// If no NoteRow existed before, try creating one
							if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
								modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, yDisplay);
							}

							if (modelStackWithNoteRow->getNoteRowAllowNull()) {
								getCurrentInstrumentClip()->recordNoteOn(modelStackWithNoteRow,
								                                         getCurrentInstrument()->defaultVelocity);
							}
						}

						sendAuditionNote(
						    true, yDisplay, lastAuditionedVelocityOnScreen[yDisplay],
						    0); // Should this technically grab the note-length of the note if there is one?
					}
				}
				else {
					auditionPadIsPressed[yDisplay] = false;
					lastAuditionedVelocityOnScreen[yDisplay] = 255;
					forceStoppedAnyAuditioning = true;
				}
			}
			if (!draggingNoteRow && !drawnNoteCodeYet
			    && auditionPadIsPressed
			        [yDisplay]) { // If we're shiftingNoteRow, no need to re-draw the noteCode, because it'll be the same
				drawNoteCode(yDisplay);
				if (isKit) {
					Drum* newSelectedDrum = NULL;
					NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);
					if (noteRow) {
						newSelectedDrum = noteRow->drum;
					}
					setSelectedDrum(newSelectedDrum, true);
					changedActiveModControllable = !getAffectEntire();
				}

				if (getCurrentOutputType() == OutputType::SYNTH) {
					if (getCurrentUI() == &soundEditor
					    && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
						menu_item::multiRangeMenu.noteOnToChangeRange(
						    getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
						    + ((SoundInstrument*)getCurrentOutput())->transpose);
					}
				}

				drawnNoteCodeYet = true;
			}
		}
	}
	if (forceStoppedAnyAuditioning) {
		someAuditioningHasEnded(true);
	}

	// If presses happening, place the Notes on the newly-aligned NoteRows
	if (numEditPadPresses > 0) {

		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		//if (!action) return; // Couldn't happen?

		action->updateYScrollClipViewAfter(getCurrentInstrumentClip());

		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				// Try getting existing NoteRow. If none...
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getCurrentInstrumentClip()->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);
				if (!modelStackWithNoteRow->getNoteRowAllowNull()) {

					if (isKit) {
						goto cancelPress;
					}

					// Try creating NoteRow
					modelStackWithNoteRow = createNoteRowForYDisplay(modelStack, editPadPresses[i].yDisplay);

					if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
						display->displayError(ERROR_INSUFFICIENT_RAM);
cancelPress:
						endEditPadPress(i);
						continue;
					}
				}

				NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();

				int32_t pos = editPadPresses[i].intendedPos;

				bool success =
				    noteRow->attemptNoteAdd(pos, editPadPresses[i].intendedLength, editPadPresses[i].intendedVelocity,
				                            editPadPresses[i].intendedProbability, modelStackWithNoteRow, action);

				editPadPresses[i].deleteOnDepress = false;
				editPadPresses[i].deleteOnScroll = success;

				if (success) {
					if (editPadPresses[i].mpeCachedYet) {
						int32_t anyActualNodes = 0;
						for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
							anyActualNodes += editPadPresses[i].stolenMPE[m].num;
						}

						if (anyActualNodes) {
							noteRow->paramManager.ensureExpressionParamSetExists(
							    isKit); // If this fails, we'll detect that below.
						}

						ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
						ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;

						if (mpeParams) {
							ModelStackWithParamCollection* modelStackWithParamCollection =
							    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
							        mpeParams, mpeParamsSummary);

							int32_t distanceToNextNote = noteRow->getDistanceToNextNote(pos, modelStackWithNoteRow);
							int32_t loopLength = modelStackWithNoteRow->getLoopLength();

							for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
								AutoParam* param = &mpeParams->params[m];
								ModelStackWithAutoParam* modelStackWithAutoParam =
								    modelStackWithParamCollection->addAutoParam(m, param);

								param->insertStolenNodes(modelStackWithAutoParam, pos, distanceToNextNote, loopLength,
								                         action, &editPadPresses[i].stolenMPE[m]);
							}
						}
					}
				}
			}
		}
		checkIfAllEditPadPressesEnded(false); // Don't allow to redraw sidebar - it's going to be redrawn below anyway
	}

	uiNeedsRendering(this); // Might be in waveform view
	return ActionResult::DEALT_WITH;
}

void InstrumentClipView::reassessAllAuditionStatus() {
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		reassessAuditionStatus(yDisplay);
	}
}

void InstrumentClipView::reassessAuditionStatus(uint8_t yDisplay) {
	uint32_t sampleSyncLength;
	uint8_t newVelocity = getVelocityForAudition(yDisplay, &sampleSyncLength);
	// If some change in the NoteRow's audition status (it's come on or off or had its velocity changed)...
	if (newVelocity != lastAuditionedVelocityOnScreen[yDisplay]) {

		// Switch note off if it was on
		if (lastAuditionedVelocityOnScreen[yDisplay] != 255) {
			sendAuditionNote(false, yDisplay, 127, 0);
		}

		// Switch note on if we want it on (it may have a different velocity now)
		if (newVelocity != 255) {
			sendAuditionNote(true, yDisplay, newVelocity, sampleSyncLength);
		}

		lastAuditionedVelocityOnScreen[yDisplay] = newVelocity;
	}
}

// This may send it on a different Clip, if a different one is the activeClip
void InstrumentClipView::sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength) {
	Instrument* instrument = getCurrentInstrument();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (instrument->type == OutputType::KIT) {
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(getCurrentInstrumentClip());
		ModelStackWithNoteRow* modelStackWithNoteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(
		    yDisplay, modelStackWithTimelineCounter); // On *current* clip!

		NoteRow* noteRowOnCurrentClip = modelStackWithNoteRow->getNoteRowAllowNull();

		// There may be no NoteRow at all if a different Clip than the one we're viewing is the activeClip, and it can't be changed
		if (noteRowOnCurrentClip) {

			Drum* drum = noteRowOnCurrentClip->drum;

			if (drum) {

				if (getCurrentClip() != instrument->activeClip) {
					modelStackWithTimelineCounter->setTimelineCounter(instrument->activeClip);
					modelStackWithNoteRow =
					    ((InstrumentClip*)instrument->activeClip)
					        ->getNoteRowForDrum(modelStackWithTimelineCounter, drum); // On *active* clip!
					if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
						return;
					}
				}

				if (on) {
					if (drum->type == DrumType::SOUND
					    && !modelStackWithNoteRow->getNoteRow()->paramManager.containsAnyMainParamCollections()) {
						FREEZE_WITH_ERROR("E325"); // Trying to catch an E313 that Vinz got
					}
					((Kit*)instrument)->beginAuditioningforDrum(modelStackWithNoteRow, drum, velocity, zeroMPEValues);
				}
				else {
					((Kit*)instrument)->endAuditioningForDrum(modelStackWithNoteRow, drum);
				}
			}
		}
	}
	else {
		int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);

		if (on) {
			((MelodicInstrument*)instrument)
			    ->beginAuditioningForNote(modelStack, yNote, velocity, zeroMPEValues, MIDI_CHANNEL_NONE,
			                              sampleSyncLength);
		}
		else {
			((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, yNote);
		}
	}
}

uint8_t InstrumentClipView::getVelocityForAudition(uint8_t yDisplay, uint32_t* sampleSyncLength) {
	int32_t numInstances = 0;
	uint32_t sum = 0;
	*sampleSyncLength = 0;
	if (auditionPadIsPressed[yDisplay] && !auditioningSilently) {
		sum += getCurrentInstrument()->defaultVelocity;
		numInstances++;
	}
	if (!playbackHandler.playbackState && numEditPadPressesPerNoteRowOnScreen[yDisplay] > 0) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		if (makeCurrentClipActiveOnInstrumentIfPossible(modelStack)) { // Should always be true, cos playback is stopped

			for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
				if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay) {
					sum += editPadPresses[i].intendedVelocity;
					numInstances++;
					*sampleSyncLength = editPadPresses[i].intendedLength;
				}
			}
		}
	}

	if (numInstances == 0) {
		return 255;
	}
	return sum / numInstances;
}

uint8_t InstrumentClipView::getNumNoteRowsAuditioning() {
	uint8_t num = 0;
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (auditionPadIsPressed[i]) {
			num++;
		}
	}
	return num;
}

uint8_t InstrumentClipView::oneNoteAuditioning() {
	return (currentUIMode == UI_MODE_AUDITIONING && getNumNoteRowsAuditioning() == 1);
}

void InstrumentClipView::setRowProbability(int32_t offset) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithNoteRow* modelStackWithNoteRow = getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

	NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

	// If we're in Kit mode, the NoteRow will exist, or else we wouldn't be auditioning it. But if in other mode, we need to do this
	if (!noteRow) {
		return; // Get out if NoteRow doesn't exist and can't be created
	}

	uint8_t probability = noteRow->probabilityValue;
	int32_t probabilityValue = probability & 127;
	bool prevBase = (probability & 128);

	// If editing, continue edit
	if (display->hasPopupOfType(DisplayPopupType::PROBABILITY)) {
		Action* action = actionLogger.getNewAction(ACTION_NOTE_EDIT, true);
		if (!action) {
			return;
		}

		action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
		                                                     modelStackWithNoteRow->noteRowId, &noteRow->notes,
		                                                     false); // Snapshot for undoability. Don't steal data.

		// Covers the probabilities and iterations and the special case of Not Fill
		// Incrementing
		if (offset == 1) {
			if (probabilityValue == 0) {
				// From NOT FILL (value: 0 | 128) we go up to 5% (value: 1)
				if (prevBase) {
					probabilityValue = 1;
					prevBase = false;
				}
				// From FILL (value: 0) we go up to NOT FILL (value: 0 | 128)
				else {
					prevBase = true;
				}
			}
			// In any other case we just increment probability value
			else if (probabilityValue < kNumProbabilityValues + kNumIterationValues) {
				probabilityValue++;
				// As we are treating multiple notes, we need to reset prevBase and remove the "latching" state for leftMostNote
				prevBase = false;
			}
		}
		// Decrementing
		else {
			if (probabilityValue == 1) {
				// From 5% (value: 1) we go down to NOT FILL (value: 0 | 128)
				prevBase = true;
				probabilityValue = 0;
			}
			else if (probabilityValue == 0 && prevBase) {
				// From NOT FILL (value: 0 | 128) we go down to FILL (value: 0)
				prevBase = false;
			}
			// In any other case we just increment probability value
			else if (probabilityValue > 1) {
				probabilityValue--;
				// As we are treating multiple notes, we need to reset prevBase and remove the "latching" state for leftMostNote
				prevBase = false;
			}
		}

		uint8_t probabilityForFow = probabilityValue;
		if (prevBase) {
			probabilityForFow |= 128;
		}
		noteRow->probabilityValue = probabilityForFow;

		uint32_t numNotes = noteRow->notes.getNumElements();
		for (int i = 0; i < numNotes; i++) {
			Note* note = noteRow->notes.getElement(i);
			note->setProbability(probabilityForFow);
		}
	}
	displayProbability(probabilityValue, prevBase);
}

// GCC is fine with 29 or 5 for the size, but does not like that it could be either
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="

void InstrumentClipView::displayProbability(uint8_t probability, bool prevBase) {
	char buffer[(display->haveOLED()) ? 29 : 5];

	sprintf(buffer, "P %d %d", probability, prevBase);
	// FILL mode
	if (probability == kFillProbabilityValue && !prevBase) {
		strcpy(buffer, "FILL");
	}

	// NO-FILL mode
	else if (probability == kFillProbabilityValue && prevBase) {
		strcpy(buffer, "NOT FILL");
	}

	// Probability dependence
	else if (probability <= kNumProbabilityValues) {
		if (display->haveOLED()) {
			sprintf(buffer, "Probability %d%%", probability * 5);
			if (prevBase) {
				strcat(buffer, " latching");
			}
		}
		if (display->have7SEG()) {
			intToString(probability * 5, buffer);
		}
	}

	// Iteration dependence
	else {

		int32_t divisor, iterationWithinDivisor;
		dissectIterationDependence(probability, &divisor, &iterationWithinDivisor);

		int32_t charPos = 0;

		sprintf(buffer, ((display->haveOLED() == 1) ? "Iteration dependence: %d of %d" : "%dof%d"),
		        iterationWithinDivisor + 1, divisor);
	}

	if (display->haveOLED()) {
		display->popupText(buffer, DisplayPopupType::PROBABILITY);
	}
	if (display->have7SEG()) {
		display->displayPopup(buffer, 0, true, prevBase ? 3 : 255, 1, DisplayPopupType::PROBABILITY);
	}
}
#pragma gcc pop

void InstrumentClipView::offsetNoteCodeAction(int32_t newOffset) {

	actionLogger.deleteAllLogs(); // Can't undo past this!

	uint8_t yVisualWithinOctave;

	// If in scale mode, need to check whether we're allowed to change scale..
	if (getCurrentInstrumentClip()->isScaleModeClip()) {
		newOffset = std::clamp<int32_t>(newOffset, -1, 1);
		yVisualWithinOctave = getYVisualWithinOctaveFromYDisplay(lastAuditionedYDisplay);

		// If not allowed to move, blink the scale mode button to remind the user that that's why
		if (!currentSong->mayMoveModeNote(yVisualWithinOctave, newOffset)) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::SCALE_MODE);
			int32_t noteCode = getCurrentInstrumentClip()->getYNoteFromYDisplay(lastAuditionedYDisplay, currentSong);
			drawActualNoteCode(noteCode); // Draw it again so that blinking stops temporarily
			return;
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	ModelStackWithNoteRow* modelStackWithNoteRow = getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

	NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

	// If we're in Kit mode, the NoteRow will exist, or else we wouldn't be auditioning it. But if in other mode, we need to do this
	if (!noteRow) {
		return; // Get out if NoteRow doesn't exist and can't be created
	}

	// Stop current note-sound from the NoteRow in question
	if (playbackHandler.isEitherClockActive()) {
		noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	}

	// Stop the auditioning
	auditionPadIsPressed[lastAuditionedYDisplay] = false;
	reassessAuditionStatus(lastAuditionedYDisplay);

	if (getCurrentOutputType() != OutputType::KIT) {
		// If in scale mode, edit the scale
		if (getCurrentInstrumentClip()->inScaleMode) {
			currentSong->changeMusicalMode(yVisualWithinOctave, newOffset);
			// If we're shifting the root note, compensate scrolling
			if (yVisualWithinOctave == 0) {
				getCurrentInstrumentClip()->yScroll += newOffset;
			}
			recalculateColour(lastAuditionedYDisplay); // RGB will have changed slightly

doRenderRow:
			uiNeedsRendering(this, 1 << lastAuditionedYDisplay, 0);
		}

		// Otherwise, can't do anything - give error
		else {
			indicator_leds::indicateAlertOnLed(IndicatorLED::SCALE_MODE);
		}
	}

	// Switch Drums, if we're in Kit mode
	else {
		Drum* oldDrum = noteRow->drum;
		Drum* newDrum = flipThroughAvailableDrums(newOffset, oldDrum);

		if (oldDrum) {
			oldDrum->drumWontBeRenderedForAWhile();
		}

		noteRow->setDrum(newDrum, getCurrentKit(), modelStackWithNoteRow);
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
		setSelectedDrum(newDrum, true);
		goto doRenderRow;
	}

	// Restart the auditioning
	auditionPadIsPressed[lastAuditionedYDisplay] = true;
	reassessAuditionStatus(lastAuditionedYDisplay);

	// Redraw the NoteCode
	drawNoteCode(lastAuditionedYDisplay);

	uiNeedsRendering(this, 0, 1 << lastAuditionedYDisplay);
}

Drum* InstrumentClipView::flipThroughAvailableDrums(int32_t newOffset, Drum* drum, bool mayBeNone) {

	Drum* startedAtDrum = drum;
	Drum* newDrum = startedAtDrum;

	if (newOffset >= 0) {
		while (true) {
			newDrum = getNextDrum(newDrum, mayBeNone);
			// Keep going until we get back to where we started, or we're on "none" or "new", or we find an unused Drum.
			if (newDrum == startedAtDrum || newDrum == NULL || newDrum == (Drum*)0xFFFFFFFF
			    || !getCurrentInstrumentClip()->getNoteRowForDrum(newDrum)) {
				break;
			}
		}
	}
	else {
		Drum* lookAheadDrum = startedAtDrum;

		while (true) {
			lookAheadDrum = getNextDrum(lookAheadDrum, mayBeNone);
			// Keep going until we get back to where we started
			if (lookAheadDrum == startedAtDrum) {
				break;
			}

			if (lookAheadDrum == NULL || lookAheadDrum == (Drum*)0xFFFFFFFF
			    || !getCurrentInstrumentClip()->getNoteRowForDrum(lookAheadDrum)) {
				newDrum = lookAheadDrum;
			}
		}
	}
	return newDrum;
}

Drum* InstrumentClipView::getNextDrum(Drum* oldDrum, bool mayBeNone) {
	if (oldDrum == NULL) {
		Drum* newDrum = getCurrentKit()->firstDrum;
		/*
    	if (newDrum == NULL) {
    		newDrum = (Drum*)0xFFFFFFFF;
    	}
    	*/
		return newDrum;
	}
	//if (oldDrum == (Drum*)0xFFFFFFFF) return NULL;

	Drum* nextDrum = ((SoundDrum*)oldDrum)->next;
	//if (!nextDrum) nextDrum = (Drum*)0xFFFFFFFF;
	return nextDrum;
}

int32_t InstrumentClipView::getYVisualFromYDisplay(int32_t yDisplay) {
	return yDisplay + getCurrentInstrumentClip()->yScroll;
}

int32_t InstrumentClipView::getYVisualWithinOctaveFromYDisplay(int32_t yDisplay) {
	int32_t yVisual = getYVisualFromYDisplay(yDisplay);
	int32_t yVisualRelativeToRoot = yVisual - currentSong->rootNote;
	int32_t yVisualWithinOctave = yVisualRelativeToRoot % currentSong->numModeNotes;
	if (yVisualWithinOctave < 0) {
		yVisualWithinOctave += currentSong->numModeNotes;
	}
	return yVisualWithinOctave;
}

// Beware - supplying shouldRedrawStuff as false will cause the activeModControllable to *not* update! Probably never should do this anymore...
void InstrumentClipView::setSelectedDrum(Drum* drum, bool shouldRedrawStuff, Kit* selectedKit) {
	Clip* clip = getCurrentClip();
	//check if you've already selected this drum
	Kit* kit;
	if (selectedKit) {
		kit = selectedKit;
	}
	else {
		kit = (Kit*)clip->output;
	}
	UI* currentUI = getCurrentUI();

	bool drumSelectionChanged = false;

	//is the drum the same as the currently selected drum in the kit?
	//if so, no need to reselect it or redraw the clip or resend midi feedback
	//if no, update selected drum
	if (kit->selectedDrum != drum) {
		if (currentUI != &soundEditor && currentUI != &sampleBrowser && currentUI != &sampleMarkerEditor
		    && currentUI != &renameDrumUI) {

			kit->selectedDrum = drum;
			drumSelectionChanged = true;
		}
	}

	if (shouldRedrawStuff) {
		//make sure we're dealing with the same clip that this kit is a part of
		//if you selected a clip and then sent a midi note to a kit that is part of a different clip, well
		//we don't need to do anything here because we're in a different clip
		if (clip == kit->activeClip) {
			//let's make sure that that the output type for that clip is a kit
			//(if for some strange reason you changed the drum selection for a hibernated instrument...)
			if (clip->output->type == OutputType::KIT) {
				//are we currently in the instrument clip UI?
				//if yes, we may need to refresh it (main pads and / or sidebar)
				if (currentUI == &instrumentClipView || currentUI == &automationClipView) {
					bool affectEntire = ((InstrumentClip*)clip)->affectEntire;

					//don't reset mod controllable when affect entire is enabled because mod controllable is unchanged
					//(you can't control the newly selected row's model stack with gold encoders when affect entire is enabled)
					//no need to potentially send midi follow feedback either because context hasn't changed
					if (!affectEntire && drumSelectionChanged) {
						//reset mod controllable stack / send midi feedback
						//redraw mod (gold) encoder led indicators
						view.setActiveModControllableTimelineCounter(clip);
					}

					//if in automation clip view with affect entire disabled
					//redraw main pads (go back to overview) + sidebar
					if (currentUI == &automationClipView && !affectEntire && drumSelectionChanged) {
						automationClipView.initParameterSelection();
						uiNeedsRendering(currentUI);
					}
					//if in instrument clip view
					//or automation clip view (with affect entire disabled)
					//or just auditioning the same drum selection
					//redraw sidebar
					else {
						renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
					}
				}
			}
		}
	}
}

void InstrumentClipView::auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown) {
	if (editedAnyPerNoteRowStuffSinceAuditioningBegan && !velocity) {
		//in case we were editing quantize/humanize
		actionLogger.closeAction(ACTION_NOTE_NUDGE);
	}
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	bool clipIsActiveOnInstrument = makeCurrentClipActiveOnInstrumentIfPossible(modelStack);

	Instrument* instrument = getCurrentInstrument();

	bool isKit = (instrument->type == OutputType::KIT);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
	    getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, modelStackWithTimelineCounter);

	Drum* drum = NULL;

	// If Kit...
	if (isKit) {

		if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
			drum = modelStackWithNoteRowOnCurrentClip->getNoteRow()->drum;
		}

		// If NoteRow doesn't exist here, we'll see about creating one
		else {

			// But not if we're actually not on this screen
			if (getCurrentUI() != this) {
				return;
			}

			// Press-down
			if (velocity) {

				setSelectedDrum(NULL);

				if (currentUIMode == UI_MODE_NONE) {
					currentUIMode = UI_MODE_ADDING_DRUM_NOTEROW;
					fileBrowserShouldNotPreview = shiftButtonDown;

					drumForNewNoteRow = NULL; //(Drum*)0xFFFFFFFF;
					//newDrumOptionSelected = true;
					drawDrumName(drumForNewNoteRow);

					// Remember what NoteRow was pressed - and limit to being no further than 1 above or 1 below the existing NoteRows
					yDisplayOfNewNoteRow = yDisplay;
					yDisplayOfNewNoteRow =
					    std::max((int32_t)yDisplayOfNewNoteRow, (int32_t)-1 - getCurrentInstrumentClip()->yScroll);
					int32_t maximum =
					    getCurrentInstrumentClip()->getNumNoteRows() - getCurrentInstrumentClip()->yScroll;
					yDisplayOfNewNoteRow = std::min((int32_t)yDisplayOfNewNoteRow, maximum);

					goto justReRender;
				}
			}

			// Press-up
			else {
				if (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW) {
					currentUIMode = UI_MODE_NONE;

					// If the user didn't select "none"...
					if (drumForNewNoteRow) {

						// Make a new NoteRow
						int32_t noteRowIndex;
						NoteRow* newNoteRow =
						    createNewNoteRowForKit(modelStackWithTimelineCounter, yDisplayOfNewNoteRow, &noteRowIndex);
						if (newNoteRow) {
							uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);

							ModelStackWithNoteRow* modelStackWithNoteRow =
							    modelStackWithTimelineCounter->addNoteRow(noteRowIndex, newNoteRow);
							newNoteRow->setDrum(drumForNewNoteRow, (Kit*)instrument, modelStackWithNoteRow);
							AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
						}
					}
					if (display->haveOLED()) {
						deluge::hid::display::OLED::removePopup();
					}
					else {
						redrawNumericDisplay();
					}
justReRender:
					uiNeedsRendering(this, 0, 1 << yDisplayOfNewNoteRow);
				}
			}

			goto getOut;
		}
	}

	// Or if synth
	else if (instrument->type == OutputType::SYNTH) {
		if (velocity) {
			if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
				menu_item::multiRangeMenu.noteOnToChangeRange(
				    getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong)
				    + ((SoundInstrument*)instrument)->transpose);
			}
		}
	}

	// Recording - only allowed if currentClip is activeClip
	if (clipIsActiveOnInstrument && playbackHandler.shouldRecordNotesNow()
	    && currentSong->isClipActive(getCurrentClip())) {

		// Note-on
		if (velocity) {

			// If count-in is on, we only got here if it's very nearly finished, so pre-empt that note.
			// This is basic. For MIDI input, we do this in a couple more cases - see noteMessageReceived()
			// in MelodicInstrument and Kit
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				if (isKit) {
					if (drum) {
						drum->recordNoteOnEarly(
						    (velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity : velocity,
						    getCurrentInstrumentClip()->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
					}
				}
				else {
					// NoteRow is allowed to be NULL in this case.
					int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					((MelodicInstrument*)instrument)
					    ->earlyNotes.insertElementIfNonePresent(
					        yNote, instrument->defaultVelocity,
					        getCurrentInstrumentClip()->allowNoteTails(modelStackWithNoteRowOnCurrentClip));
				}
			}

			else {

				// May need to create NoteRow if there wasn't one previously
				if (!modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

					modelStackWithNoteRowOnCurrentClip =
					    createNoteRowForYDisplay(modelStackWithTimelineCounter, yDisplay);
				}

				if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
					getCurrentInstrumentClip()->recordNoteOn(
					    modelStackWithNoteRowOnCurrentClip,
					    (velocity == USE_DEFAULT_VELOCITY) ? instrument->defaultVelocity : velocity);
					goto maybeRenderRow;
				}
			}
		}

		// Note-off
		else {

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {
				getCurrentInstrumentClip()->recordNoteOff(modelStackWithNoteRowOnCurrentClip);
maybeRenderRow:
				if (!(currentUIMode & UI_MODE_HORIZONTAL_SCROLL)) { // What about zoom too?
					uiNeedsRendering(this, 1 << yDisplay, 0);
				}
			}
		}
	}

	{
		NoteRow* noteRowOnActiveClip;

		if (clipIsActiveOnInstrument) {
			noteRowOnActiveClip = modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull();
		}

		else {
			// Kit
			if (instrument->type == OutputType::KIT) {
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForDrum(drum);
			}

			// Non-kit
			else {
				int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
				noteRowOnActiveClip = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(yNote);
			}
		}

		// If note on...
		if (velocity) {
			int32_t velocityToSound = velocity;
			if (velocityToSound == USE_DEFAULT_VELOCITY) {
				velocityToSound = getCurrentInstrument()->defaultVelocity;
			}

			auditionPadIsPressed[yDisplay] =
			    velocityToSound; // Yup, need to do this even if we're going to do a "silent" audition, so pad lights up etc.

			if (noteRowOnActiveClip) {
				// Ensure our auditioning doesn't override a note playing in the sequence
				if (playbackHandler.isEitherClockActive()
				    && noteRowOnActiveClip->soundingStatus == STATUS_SEQUENCED_NOTE) {
					goto doSilentAudition;
				}
			}

			// If won't be actually sounding Instrument...
			if (shiftButtonDown || Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {

				fileBrowserShouldNotPreview = true;
doSilentAudition:
				auditioningSilently = true;
				reassessAllAuditionStatus();
			}
			else {
				if (!auditioningSilently) {

					fileBrowserShouldNotPreview = false;

					sendAuditionNote(true, yDisplay, velocityToSound, 0);

					{ lastAuditionedVelocityOnScreen[yDisplay] = velocityToSound; }
				}
			}

			// If wasn't already auditioning...
			if (!isUIModeActive(UI_MODE_AUDITIONING)) {
				shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
				editedAnyPerNoteRowStuffSinceAuditioningBegan = false;
				enterUIMode(UI_MODE_AUDITIONING);
			}

			drawNoteCode(yDisplay);
			lastAuditionedYDisplay = yDisplay;

			// Begin resampling / output-recording
			if (Buttons::isButtonPressed(deluge::hid::button::RECORD)
			    && audioRecorder.recordingSource == AudioInputChannel::NONE) {
				audioRecorder.beginOutputRecording();
				Buttons::recordButtonPressUsedUp = true;
			}

			if (isKit) {
				setSelectedDrum(drum);
				goto getOut; // No need to redraw any squares, because setSelectedDrum() has done it
			}
		}

		// Or if auditioning this NoteRow just finished...
		else {
			if (auditionPadIsPressed[yDisplay]) {
				auditionPadIsPressed[yDisplay] = 0;
				lastAuditionedVelocityOnScreen[yDisplay] = 255;

				// Stop the note sounding - but only if a sequenced note isn't in fact being played here.
				if (!noteRowOnActiveClip || noteRowOnActiveClip->soundingStatus == STATUS_OFF) {
					sendAuditionNote(false, yDisplay, 64, 0);
				}
			}
			display->cancelPopup();        // In case euclidean stuff was being edited etc
			someAuditioningHasEnded(true); //lastAuditionedYDisplay == yDisplay);
			actionLogger.closeAction(ACTION_EUCLIDEAN_NUM_EVENTS_EDIT);
			actionLogger.closeAction(ACTION_NOTEROW_ROTATE);
		}

		renderingNeededRegardlessOfUI(0, 1 << yDisplay);
	}

getOut:

	// This has to happen after setSelectedDrum is called, cos that resets LEDs
	if (!clipIsActiveOnInstrument && velocity) {
		indicator_leds::indicateAlertOnLed(IndicatorLED::SESSION_VIEW);
	}
}

void InstrumentClipView::cancelAllAuditioning() {
	if (isUIModeActive(UI_MODE_AUDITIONING)) {
		memset(auditionPadIsPressed, 0, sizeof(auditionPadIsPressed));
		reassessAllAuditionStatus();
		exitUIMode(UI_MODE_AUDITIONING);
		uiNeedsRendering(this, 0, 0xFFFFFFFF);
	}
}

void InstrumentClipView::enterDrumCreator(ModelStackWithNoteRow* modelStack, bool doRecording) {

	D_PRINTLN("enterDrumCreator");

	char const* prefix;
	String soundName;

	if (doRecording) {
		prefix = "TEM"; // Means "temp". Actual "REC" name is set in audioRecorder
	}
	else {
		prefix = "U";
	}

	soundName.set(prefix);

	Kit* kit = (Kit*)modelStack->song->currentClip->output;

	int32_t error = kit->makeDrumNameUnique(&soundName, 1);
	if (error) {
doDisplayError:
		display->displayError(error);
		return;
	}

	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SoundDrum));
	if (!memory) {
		error = ERROR_INSUFFICIENT_RAM;
		goto doDisplayError;
	}

	ParamManagerForTimeline paramManager;
	error = paramManager.setupWithPatching();
	if (error) {
		delugeDealloc(memory);
		goto doDisplayError;
	}

	Sound::initParams(&paramManager);
	SoundDrum* newDrum = new (memory) SoundDrum();
	newDrum->setupAsSample(&paramManager);

	modelStack->song->backUpParamManager(newDrum, modelStack->song->currentClip, &paramManager, true);

	newDrum->name.set(&soundName);
	newDrum->nameIsDiscardable = true;

	kit->addDrum(newDrum);
	modelStack->getNoteRow()->setDrum(newDrum, kit,
	                                  modelStack); // Sets noteRow->paramManager to newDrum->backedUpParamManager

	kit->beenEdited();

	setSelectedDrum(newDrum); // Does this really need to render?

	// Can't fail because we just set the selected Drum
	// TODO: what if fail because no RAM
	bool success = soundEditor.setup(getCurrentInstrumentClip(), &menu_item::fileSelectorMenu, 0);

	if (doRecording) {
		success = openUI(&audioRecorder);
		if (success) {
			audioRecorder.process();
		}
	}
	else {
		success = openUI(&sampleBrowser);
		if (success) {
			PadLEDs::
			    skipGreyoutFade(); // Greyout can't be done at same time as horizontal scroll, which is now happening probably.
			PadLEDs::sendOutSidebarColoursSoon();
		}
	}

	if (!success) {
		openUI(&soundEditor);
	}
}

void InstrumentClipView::deleteDrum(SoundDrum* drum) {

	Kit* kit = getCurrentKit();

	kit->removeDrum(drum);

	// Find Drum's NoteRow
	int32_t noteRowIndex;
	NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowForDrum(drum, &noteRowIndex);
	if (noteRow) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);

		// Give NoteRow another unassigned Drum, or no Drum if there are none
		noteRow->setDrum(kit->getFirstUnassignedDrum(getCurrentInstrumentClip()), kit, modelStackWithNoteRow);

		if (!noteRow->drum) {
			// If NoteRow has no Notes, just delete it - if it's not the last one
			if (noteRow->hasNoNotes() && getCurrentInstrumentClip()->getNumNoteRows() > 1) {
				if (noteRowIndex == 0) {
					getCurrentInstrumentClip()->yScroll--;
				}

				getCurrentInstrumentClip()->deleteNoteRow(modelStack, noteRowIndex);
			}
		}
	}

	// Delete the Drum we came here to delete
	currentSong->deleteBackedUpParamManagersForModControllable(drum);
	void* toDealloc = dynamic_cast<void*>(drum);
	drum->~SoundDrum();
	delugeDealloc(toDealloc);

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

	// We should repopulateNoteRowsOnscreen() and everything, but this will only be called just before the UI sessions starts again anyway
}

void InstrumentClipView::someAuditioningHasEnded(bool recalculateLastAuditionedNoteOnScreen) {
	// Try to find another auditioned NoteRow so we can show its name etc
	int32_t i;
	for (i = 0; i < kDisplayHeight; i++) {
		if (auditionPadIsPressed[i]) {
			// Show this note's noteCode, if the noteCode we were showing before is the note we just stopped auditioning
			if (recalculateLastAuditionedNoteOnScreen) {
				drawNoteCode(i);
				lastAuditionedYDisplay = i;
			}
			break;
		}
	}

	// Or, if all auditioning now finished...
	if (i == kDisplayHeight) {
		exitUIMode(UI_MODE_AUDITIONING);
		auditioningSilently = false;

		//check that you're not in automation instrument clip view and holding an automation pad down
		//if not, clear popup's / re-draw screen
		if (!((getCurrentUI() == &automationClipView) && isUIModeActive(UI_MODE_NOTES_PRESSED))) {
			if (display->haveOLED()) {
				deluge::hid::display::OLED::removePopup();
			}
			else {
				redrawNumericDisplay();
			}
		}
	}
}

void InstrumentClipView::drawNoteCode(uint8_t yDisplay) {
	// Might not want to actually do this...
	if (!getCurrentUI()->toClipMinder()) {
		return;
	}

	if (getCurrentOutputType() != OutputType::KIT) {
		drawActualNoteCode(getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong));
	}
	else {
		drawDrumName(getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong)->drum);
	}
}

void InstrumentClipView::drawDrumName(Drum* drum, bool justPopUp) {

	char const* newText;

	if (display->haveOLED()) {
		char buffer[50];

		if (!drum) {
			newText = "No sound";
		}
		else if (drum->type == DrumType::SOUND) {
			newText = ((SoundDrum*)drum)->name.get();
		}
		else {
			newText = buffer;

			if (drum->type == DrumType::GATE) {

				strcpy(buffer, "Gate channel ");
				intToString(((GateDrum*)drum)->channel + 1, &buffer[13]);
				indicator_leds::blinkLed(IndicatorLED::CV, 1, 1);
			}
			else { // MIDI
				char topLine[30];
				char noteLabel[5];

				sprintf(topLine, "CH: %d  N#: %d", ((MIDIDrum*)drum)->channel + 1, ((MIDIDrum*)drum)->note);
				noteCodeToString(((MIDIDrum*)drum)->note, noteLabel);

				const char* lines[] = {topLine, noteLabel};

				char bufferLines[50];
				concatenateLines(lines, sizeof(lines) / sizeof(lines[0]), buffer);
				indicator_leds::blinkLed(IndicatorLED::MIDI, 1, 1);
			}
		}

		display->popupText(newText);
	}
	else {

		char buffer[7];

		if (!drum) {
			newText = "NONE";

basicDisplay:
			if (justPopUp && currentUIMode != UI_MODE_AUDITIONING) {
				display->displayPopup(newText);
			}
			else {
				display->setText(newText, false, 255, true);
			}
		}
		else {
			if (drum->type != DrumType::SOUND) {
				drum->getName(buffer);
				newText = buffer;

				if (drum->type == DrumType::MIDI) {
					indicator_leds::blinkLed(IndicatorLED::MIDI, 1, 1);
				}
				else if (drum->type == DrumType::GATE) {
					indicator_leds::blinkLed(IndicatorLED::CV, 1, 1);
				}

				goto basicDisplay;
			}

			// If we're here, it's a SoundDrum
			SoundDrum* soundDrum = (SoundDrum*)drum;

			newText = soundDrum->name.get();
			bool andAHalf;

			if (display->getEncodedPosFromLeft(99999, newText, &andAHalf) <= kNumericDisplayLength) {
				goto basicDisplay;
			}
			display->setScrollingText(newText, 0, kInitialFlashTime + kFlashTime);
		}
	}
}

int32_t InstrumentClipView::setupForEnteringScaleMode(int32_t newRootNote, int32_t yDisplay) {
	// Having got to this function, we have recently calculated the default root note

	uiTimerManager.unsetTimer(TIMER_DEFAULT_ROOT_NOTE);
	int32_t scrollAdjust = 0;
	uint8_t pinAnimationToYDisplay;
	uint8_t pinAnimationToYNote;

	// If user manually selected what root note they want, then we've got it easy!
	if (newRootNote != 2147483647) {
		pinAnimationToYDisplay = yDisplay;
		// This is needed in case we're coming from Keyboard Screen
		pinAnimationToYNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
	}

	// Otherwise, go with the previously calculated default root note
	else {
		newRootNote = defaultRootNote;

		// If there's a root-note (or its octave) currently onscreen, pin animation to that
		for (int32_t i = 0; i < kDisplayHeight; i++) {
			int32_t thisNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(i, currentSong);
			// If it's the root note...
			if ((int32_t)std::abs(newRootNote - thisNote) % 12 == 0) {
				pinAnimationToYDisplay = i;
				pinAnimationToYNote = thisNote;
				goto doneLookingForRootNoteOnScreen;
			}
		}

		// Or if there wasn't an instance of the root note onscreen..
		pinAnimationToYDisplay = 2;
		pinAnimationToYNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(pinAnimationToYDisplay, currentSong);
	}

doneLookingForRootNoteOnScreen:

	// Need to figure out the scale first...
	getCurrentInstrumentClip()->inScaleMode = true;
	currentSong->setRootNote(newRootNote, getCurrentInstrumentClip()); // Computation to find out what notes in scale

	int32_t yVisual = getCurrentInstrumentClip()->getYVisualFromYNote(pinAnimationToYNote, currentSong);

	int32_t newScroll = yVisual - pinAnimationToYDisplay;

	getCurrentInstrumentClip()->deleteOldDrumNames();

	return newScroll;
}

void InstrumentClipView::enterScaleMode(uint8_t yDisplay) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int32_t newRootNote;
	if (yDisplay == 255) {
		newRootNote = 2147483647;
	}
	else {
		newRootNote = clip->getYNoteFromYDisplay(yDisplay, currentSong);
	}

	int32_t newScroll = setupForEnteringScaleMode(newRootNote, yDisplay);

	// See which NoteRows need to animate
	PadLEDs::numAnimatedRows = 0;
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = clip->noteRows.getElement(i);
		int32_t yVisualTo = clip->getYVisualFromYNote(thisNoteRow->y, currentSong);
		int32_t yDisplayTo = yVisualTo - newScroll;
		int32_t yDisplayFrom = thisNoteRow->y - clip->yScroll;

		// If this NoteRow is going to end up on-screen or come from on-screen...
		if ((yDisplayTo >= 0 && yDisplayTo < kDisplayHeight) || (yDisplayFrom >= 0 && yDisplayFrom < kDisplayHeight)) {

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(thisNoteRow->y, thisNoteRow);

			PadLEDs::animatedRowGoingTo[PadLEDs::numAnimatedRows] = yDisplayTo;
			PadLEDs::animatedRowGoingFrom[PadLEDs::numAnimatedRows] = yDisplayFrom;
			RGB mainColour = clip->getMainColourFromY(thisNoteRow->y, thisNoteRow->getColourOffset(clip));
			RGB tailColour = mainColour.forTail();
			RGB blurColour = mainColour.forBlur();

			thisNoteRow->renderRow(
			    this, mainColour, tailColour, blurColour, PadLEDs::imageStore[PadLEDs::numAnimatedRows],
			    PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows], true, modelStackWithNoteRow->getLoopLength(),
			    clip->allowNoteTails(modelStackWithNoteRow), kDisplayWidth, currentSong->xScroll[NAVIGATION_CLIP],
			    currentSong->xZoom[NAVIGATION_CLIP]);
			drawMuteSquare(thisNoteRow, PadLEDs::imageStore[PadLEDs::numAnimatedRows],
			               PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows]);
			PadLEDs::numAnimatedRows++;
			if (PadLEDs::numAnimatedRows >= kMaxNumAnimatedRows) {
				break;
			}
		}
	}

	PadLEDs::setupInstrumentClipCollapseAnimation(false);

	clip->yScroll = newScroll;

	displayCurrentScaleName();

	// And tidy up
	recalculateColours();
	currentUIMode = UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING;
	PadLEDs::recordTransitionBegin(kNoteRowCollapseSpeed);
	setLedStates();

	//drawAllAuditionSquares(false);

	PadLEDs::renderNoteRowExpandOrCollapse();

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_ENTER_SCALE_MODE);
}

int32_t InstrumentClipView::setupForExitingScaleMode() {

	int32_t scrollAdjust;
	// See if there's a root note onscreen
	bool foundRootNoteOnScreen = false;
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(i, currentSong);
		// If it's the root note...
		if ((int32_t)std::abs(currentSong->rootNote - yNote) % 12 == 0) {
			scrollAdjust = yNote - i - getCurrentInstrumentClip()->yScroll;
			foundRootNoteOnScreen = true;
			break;
		}
	}

	// Or if there wasn't an instance of the root note onscreen..
	if (!foundRootNoteOnScreen) {
		scrollAdjust =
		    getCurrentInstrumentClip()->getYNoteFromYVisual(getCurrentInstrumentClip()->yScroll + 1, currentSong) - 1
		    - getCurrentInstrumentClip()->yScroll;
	}

	getCurrentInstrumentClip()->inScaleMode = false;

	getCurrentInstrumentClip()->deleteOldDrumNames();

	return scrollAdjust;
}

void InstrumentClipView::exitScaleMode() {

	int32_t scrollAdjust = setupForExitingScaleMode();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	// See which NoteRows need to animate
	PadLEDs::numAnimatedRows = 0;
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = clip->noteRows.getElement(i);
		int32_t yDisplayTo = thisNoteRow->y - (clip->yScroll + scrollAdjust);
		clip->inScaleMode = true;
		int32_t yDisplayFrom = clip->getYVisualFromYNote(thisNoteRow->y, currentSong) - clip->yScroll;
		clip->inScaleMode = false;

		// If this NoteRow is going to end up on-screen or come from on-screen...
		if ((yDisplayTo >= 0 && yDisplayTo < kDisplayHeight) || (yDisplayFrom >= 0 && yDisplayFrom < kDisplayHeight)) {
			PadLEDs::animatedRowGoingTo[PadLEDs::numAnimatedRows] = yDisplayTo;
			PadLEDs::animatedRowGoingFrom[PadLEDs::numAnimatedRows] = yDisplayFrom;
			RGB mainColour = clip->getMainColourFromY(thisNoteRow->y, thisNoteRow->getColourOffset(clip));
			RGB tailColour = mainColour.forTail();
			RGB blurColour = mainColour.forBlur();

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(thisNoteRow->y, thisNoteRow);

			thisNoteRow->renderRow(
			    this, mainColour, tailColour, blurColour, PadLEDs::imageStore[PadLEDs::numAnimatedRows],
			    PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows], true, modelStackWithNoteRow->getLoopLength(),
			    clip->allowNoteTails(modelStackWithNoteRow), kDisplayWidth, currentSong->xScroll[NAVIGATION_CLIP],
			    currentSong->xZoom[NAVIGATION_CLIP]);
			drawMuteSquare(thisNoteRow, PadLEDs::imageStore[PadLEDs::numAnimatedRows],
			               PadLEDs::occupancyMaskStore[PadLEDs::numAnimatedRows]);
			PadLEDs::numAnimatedRows++;
			if (PadLEDs::numAnimatedRows >= kMaxNumAnimatedRows) {
				break;
			}
		}
	}

	clip->yScroll += scrollAdjust;

	PadLEDs::setupInstrumentClipCollapseAnimation(false);

	recalculateColours();
	currentUIMode = UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING;
	PadLEDs::recordTransitionBegin(kNoteRowCollapseSpeed);
	setLedStates();
	PadLEDs::renderNoteRowExpandOrCollapse();

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_EXIT_SCALE_MODE);
}

// If called from KeyboardScreen, the newRootNote won't correspond to the yDisplay, and that's ok
void InstrumentClipView::setupChangingOfRootNote(int32_t newRootNote, int32_t yDisplay) {
	int32_t oldYVisual = getYVisualFromYDisplay(yDisplay);
	int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYVisual(oldYVisual, currentSong);
	currentSong->setRootNote(newRootNote, getCurrentInstrumentClip()); // Computation to find out what scale etc

	int32_t newYVisual = getCurrentInstrumentClip()->getYVisualFromYNote(yNote, currentSong);
	int32_t scrollChange = newYVisual - oldYVisual;
	getCurrentInstrumentClip()->yScroll += scrollChange;
}

void InstrumentClipView::changeRootNote(uint8_t yDisplay) {

	int32_t oldYVisual = getYVisualFromYDisplay(yDisplay);
	int32_t newRootNote = getCurrentInstrumentClip()->getYNoteFromYVisual(oldYVisual, currentSong);

	setupChangingOfRootNote(newRootNote, yDisplay);
	displayCurrentScaleName();

	recalculateColours();
	uiNeedsRendering(this);

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_CHANGE_ROOT_NOTE);
}

bool InstrumentClipView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (whichRows & (1 << i)) {
			drawMuteSquare(getCurrentInstrumentClip()->getNoteRowOnScreen(i, currentSong), image[i], occupancyMask[i]);
			drawAuditionSquare(i, image[i]);
		}
	}

	return true;
}

void InstrumentClipView::drawMuteSquare(NoteRow* thisNoteRow, RGB thisImage[], uint8_t thisOccupancyMask[]) {
	RGB& thisColour = thisImage[kDisplayWidth];
	uint8_t* thisOccupancy = &(thisOccupancyMask[kDisplayWidth]);

	// If user assigning MIDI controls and this NoteRow has a command assigned, flash pink
	if (view.midiLearnFlashOn && thisNoteRow && thisNoteRow->drum
	    && thisNoteRow->drum->muteMIDICommand.containsSomething()) {
		thisColour = colours::midi_command;
		*thisOccupancy = 64;
	}

	else if (thisNoteRow == NULL || !thisNoteRow->muted) {
		if (thisNoteRow == NULL && getCurrentOutputType() == OutputType::KIT) {
			thisColour = colours::black;
		}
		else {
			thisColour = menu_item::activeColourMenu.getRGB();
		}
	}
	else {
		thisColour = menu_item::mutedColourMenu.getRGB();
		*thisOccupancy = 64;
	}

	// If user assigning MIDI controls and has this Clip selected, flash to half brightness
	if (view.midiLearnFlashOn && thisNoteRow != NULL && view.thingPressedForMidiLearn == MidiLearn::NOTEROW_MUTE
	    && thisNoteRow->drum && &thisNoteRow->drum->muteMIDICommand == view.learnedThing) {
		thisColour = thisColour.dim();
		*thisOccupancy = 64;
	}
}

bool InstrumentClipView::isRowAuditionedByInstrument(int32_t yDisplay) {
	if (getCurrentOutputType() == OutputType::KIT) {
		NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);
		if (!noteRow || !noteRow->drum) {
			return false;
		}
		return noteRow->drum->auditioned;
	}
	else {
		int32_t note = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
		return (((MelodicInstrument*)getCurrentOutput())->isNoteAuditioning(note));
	}
}

void InstrumentClipView::drawAuditionSquare(uint8_t yDisplay, RGB thisImage[]) {
	RGB& thisColour = thisImage[kDisplayWidth + 1];

	if (view.midiLearnFlashOn) {
		NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);

		bool midiCommandAssigned;
		if (getCurrentOutputType() == OutputType::KIT) {
			midiCommandAssigned = (noteRow && noteRow->drum && noteRow->drum->midiInput.containsSomething());
		}
		else {
			midiCommandAssigned = (((MelodicInstrument*)getCurrentOutput())->midiInput.containsSomething());
		}

		// If MIDI command already assigned...
		if (midiCommandAssigned) {
			thisColour = colours::midi_command;
		}

		// Or if not assigned but we're holding it down...
		else {
			bool holdingDown = false;
			if (view.thingPressedForMidiLearn == MidiLearn::MELODIC_INSTRUMENT_INPUT) {
				holdingDown = true;
			}
			else if (view.thingPressedForMidiLearn == MidiLearn::DRUM_INPUT) {
				holdingDown = (&noteRow->drum->midiInput == view.learnedThing);
			}

			if (holdingDown) {
				thisColour = rowColour[yDisplay].dim();
			}
			else {
				goto drawNormally;
			}
		}
	}

	// If audition pad pressed...
	else if (auditionPadIsPressed[yDisplay]
	         || (currentUIMode == UI_MODE_ADDING_DRUM_NOTEROW && yDisplay == yDisplayOfNewNoteRow)) {
		thisColour = rowColour[yDisplay];
		goto checkIfSelectingRanges;
	}

	else {
drawNormally:

		// Kit - draw "selected Drum"
		if (getCurrentOutputType() == OutputType::KIT) {
			NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);
			if (noteRow != NULL && noteRow->drum != NULL && noteRow->drum == getCurrentKit()->selectedDrum) {

				int32_t totalColour =
				    (uint16_t)rowColour[yDisplay][0] + rowColour[yDisplay][1] + rowColour[yDisplay][2]; // max 765

				for (int32_t colour = 0; colour < 3; colour++) {
					thisColour[colour] = ((int32_t)rowColour[yDisplay][colour] * (8421504 - 6500000)
					                      + ((int32_t)totalColour * (6500000 >> 5)))
					                     >> 23;
				}
				return;
			}
		}

		// Not kit
		else {

			if (currentUIMode == UI_MODE_SCALE_MODE_BUTTON_PRESSED) {
				if (flashDefaultRootNoteOn) {
					int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					if ((uint16_t)(yNote - defaultRootNote + 120) % (uint8_t)12 == 0) {
						thisColour = rowColour[yDisplay];
						return;
					}
				}
			}
			else {

				{
					// If this is the root note, indicate
					int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					if ((uint16_t)(yNote - currentSong->rootNote + 120) % (uint8_t)12 == 0) {
						thisColour = rowColour[yDisplay];
					}
					else {
						thisColour = colours::black;
					}
				}

checkIfSelectingRanges:
				// If we're selecting ranges...
				if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
				    || (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
					int32_t yNote = getCurrentInstrumentClip()->getYNoteFromYDisplay(yDisplay, currentSong);
					if (soundEditor.isUntransposedNoteWithinRange(yNote)) {
						thisColour = thisColour.transform([](RGB::channel_type channel) {
							return std::clamp<uint32_t>((uint32_t)channel + 30, 0, RGB::channel_max);
						});
					}
				}

				return;
			}
		}
		thisColour = colours::black;
	}
}

void InstrumentClipView::cutAuditionedNotesToOne() {
	uint32_t whichRowsNeedReRendering = 0;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (yDisplay != lastAuditionedYDisplay && auditionPadIsPressed[yDisplay]) {
			auditionPadIsPressed[yDisplay] = false;

			getCurrentInstrumentClip()->yDisplayNoLongerAuditioning(yDisplay, currentSong);

			whichRowsNeedReRendering |= (1 << yDisplay);
		}
	}
	reassessAllAuditionStatus();
	if (whichRowsNeedReRendering) {
		uiNeedsRendering(this, 0, whichRowsNeedReRendering);
	}
}

static const uint32_t verticalScrollUIModes[] = {
    UI_MODE_NOTES_PRESSED, UI_MODE_AUDITIONING, UI_MODE_RECORD_COUNT_IN, UI_MODE_DRAGGING_KIT_NOTEROW, 0,
};

ActionResult InstrumentClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {

	if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
	}

	// If encoder button pressed
	if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
		// User may be trying to move a noteCode...
		if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			/*
            if (!Buttons::isShiftButtonPressed()) { // Why'd I mandate that shift not be pressed?
                // If in kit mode, then we can do it
                if (getCurrentOutputType() == OutputType::KIT) {

                	if (inCardRoutine) return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;

                    cutAuditionedNotesToOne();
                    return scrollVertical(offset, inCardRoutine, true); // Will delete action log in this case
                }

                // Otherwise, remind the user why they can't
                else {
                    if (getCurrentOutputType() == OutputType::SYNTH) indicator_leds::indicateAlertOnLed(IndicatorLED::SYNTH);
                    else indicator_leds::indicateAlertOnLed(IndicatorLED::MIDI); // MIDI
                }
            }
            */

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

			editNumEuclideanEvents(modelStackWithNoteRow, offset, lastAuditionedYDisplay);
			shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;
			editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		}

		// Or note repeat...
		else if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
			editNoteRepeat(offset);
		}

		// If user not wanting to move a noteCode, they want to transpose the key
		else if (!currentUIMode && getCurrentOutputType() != OutputType::KIT) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			actionLogger.deleteAllLogs();

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			// If shift button not pressed, transpose whole octave
			if (!Buttons::isShiftButtonPressed()) {
				offset = std::min((int32_t)1, std::max((int32_t)-1, offset));
				getCurrentInstrumentClip()->transpose(offset * 12, modelStack);
				if (getCurrentInstrumentClip()->isScaleModeClip()) {
					getCurrentInstrumentClip()->yScroll += offset * (currentSong->numModeNotes - 12);
				}
				//display->displayPopup("OCTAVE");
			}

			// Otherwise, transpose single semitone
			else {
				// If current Clip not in scale-mode, just do it
				if (!getCurrentInstrumentClip()->isScaleModeClip()) {
					getCurrentInstrumentClip()->transpose(offset, modelStack);

					// If there are no scale-mode Clips at all, move the root note along as well - just in case the user wants to go back to scale mode (in which case the "previous" root note would be used to help guess what root note to go with)
					if (!currentSong->anyScaleModeClips()) {
						currentSong->rootNote += offset;
					}
				}

				// Otherwise, got to do all key-mode Clips
				else {
					currentSong->transposeAllScaleModeClips(offset);
				}
				//display->displayPopup("SEMITONE");
			}
		}
	}

	// Or, if shift key is pressed
	else if (Buttons::isShiftButtonPressed()) {
		uint32_t whichRowsToRender = 0;

		// If NoteRow(s) auditioned, shift its colour (Kits only)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
			if (!shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
				if (getCurrentOutputType() != OutputType::KIT) {
					goto shiftAllColour;
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
					if (auditionPadIsPressed[yDisplay]) {
						ModelStackWithNoteRow* modelStackWithNoteRow =
						    getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, modelStack);
						NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
						if (noteRow) { // This is fine. If we were in Kit mode, we could only be auditioning if there was a NoteRow already
							noteRow->colourOffset += offset;
							if (noteRow->colourOffset >= 72) {
								noteRow->colourOffset -= 72;
							}
							if (noteRow->colourOffset < 0) {
								noteRow->colourOffset += 72;
							}
							recalculateColour(yDisplay);
							whichRowsToRender |= (1 << yDisplay);
						}
					}
				}
			}
		}

		// Otherwise, adjust whole colour spectrum
		else if (currentUIMode == UI_MODE_NONE) {
shiftAllColour:
			getCurrentInstrumentClip()->colourOffset += offset;
			recalculateColours();
			whichRowsToRender = 0xFFFFFFFF;
		}

		if (whichRowsToRender) {
			uiNeedsRendering(this, whichRowsToRender, whichRowsToRender);
		}
	}

	// If neither button is pressed, we'll do vertical scrolling
	else {
		if (isUIModeWithinRange(verticalScrollUIModes)) {
			if (!shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress
			    || (!isUIModeActive(UI_MODE_NOTES_PRESSED) && !isUIModeActive(UI_MODE_AUDITIONING))) {
				bool draggingNoteRow = (isUIModeActive(UI_MODE_DRAGGING_KIT_NOTEROW));
				return scrollVertical(offset, inCardRoutine, draggingNoteRow);
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

static const uint32_t noteNudgeUIModes[] = {UI_MODE_NOTES_PRESSED, UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

ActionResult InstrumentClipView::horizontalEncoderAction(int32_t offset) {

	// If holding down notes
	if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {

		if (!Buttons::isShiftButtonPressed()) {

			// If nothing else held down, adjust velocity
			if (isUIModeActiveExclusively(UI_MODE_NOTES_PRESSED)) {
				if (!shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
					adjustVelocity(offset);
				}
			}

			// Or, if horizontal encoder held down, nudge note
			else if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
			         && isUIModeWithinRange(noteNudgeUIModes)) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
				}
				nudgeNotes(offset);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Auditioning but not holding down <> encoder - edit length of just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		if (!shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress) {
wantToEditNoteRowLength:
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    getOrCreateNoteRowForYDisplay(modelStack, lastAuditionedYDisplay);

			editNoteRowLength(modelStackWithNoteRow, offset, lastAuditionedYDisplay);
			editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		}

		// Unlike for all other cases where we protect against the user accidentally turning the encoder more after releasing their press on it,
		// for this edit-NoteRow-length action, because it's a related action, it's quite likely that the user actually will want to do it after the yes-pressed-encoder-down
		// action, which is "rotate/shift notes in row". So, we have a 250ms timeout for this one.
		else if ((uint32_t)(AudioEngine::audioSampleTimer - timeHorizontalKnobLastReleased) >= 250 * 44) {
			shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = false;
			goto wantToEditNoteRowLength;
		}
		return ActionResult::DEALT_WITH;
	}

	// Auditioning *and* holding down <> encoder - rotate/shift just one row
	else if (isUIModeActiveExclusively(UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Just be safe - maybe not necessary
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)modelStack->getTimelineCounter())
		        ->getNoteRowOnScreen(lastAuditionedYDisplay, modelStack); // Don't create

		rotateNoteRowHorizontally(modelStackWithNoteRow, offset, lastAuditionedYDisplay, true);
		shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress =
		    true; // So don't accidentally shorten row after
		editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
		return ActionResult::DEALT_WITH;
	}

	// Or, let parent deal with it
	else {
		return ClipView::horizontalEncoderAction(offset);
	}
}

void InstrumentClipView::tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed) {

	if (isUIModeActive(UI_MODE_AUDITIONING)
	    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::Quantize)
	           == RuntimeFeatureStateToggle::On) { //quantize
		if (encoderButtonPressed) {
			quantizeNotes(offset, NUDGEMODE_QUANTIZE_ALL);
		}
		else {
			quantizeNotes(offset, NUDGEMODE_QUANTIZE);
		}
	}
	else {
		playbackHandler.tempoEncoderAction(offset, encoderButtonPressed, shiftButtonPressed);
	}
}

void InstrumentClipView::quantizeNotes(int32_t offset, int32_t nudgeMode) {

	shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	//just popping up
	if (!offset) {
		quantizeAmount = 0;
		if (nudgeMode == NUDGEMODE_QUANTIZE) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_QUANTIZE));
		}
		else if (nudgeMode == NUDGEMODE_QUANTIZE_ALL) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_QUANTIZE_ALL_ROW));
		}
		return;
	}

	int32_t squareSize = getPosFromSquare(1) - getPosFromSquare(0);
	int32_t halfsquareSize = (int32_t)(squareSize / 2);
	int32_t quatersquareSize = (int32_t)(squareSize / 4);

	if (quantizeAmount >= 10 && offset > 0) {
		return;
	}
	if (quantizeAmount <= -10 && offset < 0) {
		return;
	}
	quantizeAmount += offset;
	if (quantizeAmount >= 10) {
		quantizeAmount = 10;
	}
	if (quantizeAmount <= -10) {
		quantizeAmount = -10;
	}

	if (display->haveOLED()) {
		char buffer[24];
		if (nudgeMode == NUDGEMODE_QUANTIZE) {
			strcpy(buffer, (quantizeAmount >= 0) ? "Quantize " : "Humanize ");
		}
		else {
			strcpy(buffer, (quantizeAmount >= 0) ? "Quantize All " : "Humanize All ");
		}
		intToString(abs(quantizeAmount * 10), buffer + strlen(buffer));
		strcpy(buffer + strlen(buffer), "%");
		display->popupTextTemporary(buffer);
	}
	else {
		char buffer[5];
		strcpy(buffer, "");
		intToString(quantizeAmount * 10, buffer + strlen(buffer)); //Negative means humanize
		display->displayPopup(buffer, 0, true);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	if (nudgeMode == NUDGEMODE_QUANTIZE) { // Only the row(s) being pressed

		//reset
		Action* lastAction = actionLogger.firstAction[BEFORE];
		if (lastAction && lastAction->type == ACTION_NOTE_NUDGE && lastAction->openForAdditions)
			actionLogger.undoJustOneConsequencePerNoteRow(modelStack->toWithSong());

		Action* action = NULL;
		if (offset) {
			action = actionLogger.getNewAction(ACTION_NOTE_NUDGE, ACTION_ADDITION_ALLOWED);
			if (action)
				action->offset = quantizeAmount;
		}

		for (int32_t i = 0; i < kDisplayHeight; i++) {
			if (auditionPadIsPressed[i]) {

				ModelStackWithNoteRow* modelStackWithNoteRow = getOrCreateNoteRowForYDisplay(modelStack, i);
				NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRow();
				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				if (offset) { //store
					action->recordNoteArrayChangeDefinitely(
					    (InstrumentClip*)modelStackWithNoteRow->getTimelineCounter(), modelStackWithNoteRow->noteRowId,
					    &(thisNoteRow->notes), false);
				}

				NoteVector tmpNotes;
				tmpNotes.cloneFrom(&thisNoteRow->notes); //backup
				for (int32_t j = 0; j < tmpNotes.getNumElements(); j++) {

					Note* note = tmpNotes.getElement(j);

					int32_t destination = (trunc((note->pos - 1 + halfsquareSize) / squareSize)) * squareSize;
					if (quantizeAmount < 0) { //Humanize
						int32_t hmAmout = trunc(random(quatersquareSize) - (quatersquareSize / 2.5));
						destination = note->pos + hmAmout;
					}
					int32_t distance = destination - note->pos;
					distance = trunc((distance * abs(quantizeAmount)) / 10);

					if (distance != 0) {
						for (int32_t k = 0; k < abs(distance); k++) {
							int32_t nowPos = (note->pos + ((distance > 0) ? k : -k) + noteRowEffectiveLength)
							                 % noteRowEffectiveLength;
							int32_t error = thisNoteRow->nudgeNotesAcrossAllScreens(
							    nowPos, modelStackWithNoteRow, NULL, kMaxSequenceLength, ((distance > 0) ? 1 : -1));
							if (error) {
								display->displayError(error);
								return;
							}
						}
					}
				}
			}
		}
	}
	else if (nudgeMode == NUDGEMODE_QUANTIZE_ALL) { //All Row

		//reset
		Action* lastAction = actionLogger.firstAction[BEFORE];
		if (lastAction && lastAction->type == ACTION_NOTE_NUDGE && lastAction->openForAdditions)
			actionLogger.undoJustOneConsequencePerNoteRow(modelStack->toWithSong());

		Action* action = NULL;
		if (offset) {
			action = actionLogger.getNewAction(ACTION_NOTE_NUDGE, ACTION_ADDITION_ALLOWED);
			if (action)
				action->offset = offset;
		}

		for (int32_t i = 0; i < getCurrentInstrumentClip()->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = getCurrentInstrumentClip()->noteRows.getElement(i);

			int32_t noteRowId;
			int32_t noteRowIndex;
			noteRowId = getCurrentInstrumentClip()->getNoteRowId(thisNoteRow, i);

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
			int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

			// If this NoteRow has any notes...
			if (!thisNoteRow->hasNoNotes()) {

				if (offset) { //store
					action->recordNoteArrayChangeDefinitely(
					    (InstrumentClip*)modelStackWithNoteRow->getTimelineCounter(), modelStackWithNoteRow->noteRowId,
					    &(thisNoteRow->notes), false);
				}

				NoteVector tmpNotes;
				tmpNotes.cloneFrom(&thisNoteRow->notes); //backup
				for (int32_t j = 0; j < tmpNotes.getNumElements(); j++) {
					Note* note = tmpNotes.getElement(j);

					int32_t destination = (trunc((note->pos - 1 + halfsquareSize) / squareSize)) * squareSize;
					if (quantizeAmount < 0) { //Humanize
						int32_t hmAmout = trunc(random(quatersquareSize) - (quatersquareSize / 2.5));
						destination = note->pos + hmAmout;
					}
					int32_t distance = destination - note->pos;
					distance = trunc((distance * abs(quantizeAmount)) / 10);

					if (distance != 0) {
						for (int32_t k = 0; k < abs(distance); k++) {
							int32_t nowPos = (note->pos + ((distance > 0) ? k : -k) + noteRowEffectiveLength)
							                 % noteRowEffectiveLength;
							int32_t error = thisNoteRow->nudgeNotesAcrossAllScreens(
							    nowPos, modelStackWithNoteRow, NULL, kMaxSequenceLength, ((distance > 0) ? 1 : -1));
							if (error) {
								display->displayError(error);
								return;
							}
						}
					}
				}
			}
		}
	}

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
	{
		if (playbackHandler.isEitherClockActive() && modelStack->song->currentClip->isActiveOnOutput()) {
			modelStack->song->currentClip->expectEvent();
			modelStack->song->currentClip->reGetParameterAutomation(modelStack);
		}
	}
	editedAnyPerNoteRowStuffSinceAuditioningBegan = true;
	return;
}

// Supply offset as 0 to just popup number, not change anything
void InstrumentClipView::editNoteRepeat(int32_t offset) {

	shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	if (numEditPadPresses != 1) {
		return; // Yup, you're only allowed to do this with one press at a time.
	}

	int32_t i;
	for (i = 0; i < kEditPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive) {
			break;
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* currentClip = (InstrumentClip*)modelStack->getTimelineCounter();

	ModelStackWithNoteRow* modelStackWithNoteRow =
	    currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, modelStack);

	int32_t squareStart = getPosFromSquare(editPadPresses[i].xDisplay);
	int32_t squareWidth = getSquareWidth(editPadPresses[i].xDisplay, modelStackWithNoteRow->getLoopLength());

	int32_t searchTerms[2];
	searchTerms[0] = squareStart;
	searchTerms[1] = squareStart + squareWidth;
	int32_t resultingIndexes[2];
	modelStackWithNoteRow->getNoteRow()->notes.searchDual(searchTerms, resultingIndexes);

	int32_t oldNumNotes = resultingIndexes[1] - resultingIndexes[0];
	int32_t newNumNotes = oldNumNotes + offset;

	// If "just displaying not editing" or unable to move any further, just display and get out
	if (!offset || newNumNotes < 1 || newNumNotes > squareWidth) {
		newNumNotes = oldNumNotes; // And just output that below without editing
	}

	else {
		editPadPresses[i].isBlurredSquare = true; // It's (probably) blurred now - better remember that.

		// See if we can do a "secret UNDO".
		Action* lastAction = actionLogger.firstAction[BEFORE];
		if (offset && lastAction && lastAction->type == ACTION_NOTE_REPEAT_EDIT && lastAction->openForAdditions
		    && lastAction->offset == -offset) {
			actionLogger.undoJustOneConsequencePerNoteRow(
			    modelStack
			        ->toWithSong()); // Only ok because we're not going to use the ModelStackWithTimelineCounter or with any more stuff again here.
		}

		else {
			Action* action = actionLogger.getNewAction(ACTION_NOTE_REPEAT_EDIT, ACTION_ADDITION_ALLOWED);
			if (action) {
				action->offset = offset;
			}

			modelStackWithNoteRow->getNoteRow()->editNoteRepeatAcrossAllScreens(
			    squareStart, squareWidth, modelStackWithNoteRow, action, currentClip->getWrapEditLevel(), newNumNotes);
			D_PRINTLN("did actual note repeat edit");
		}

		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		currentClip->expectEvent();
	}

	if (display->haveOLED()) {
		char buffer[20];
		strcpy(buffer, "Note repeats: ");
		intToString(newNumNotes, buffer + strlen(buffer));
		display->popupTextTemporary(buffer);
	}
	else {
		char buffer[12];
		intToString(newNumNotes, buffer);
		display->displayPopup(buffer, 0, true);
	}
}

// GCC doesn't like the MODEL_STACK_MAX_SIZE on the stack
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="

// Supply offset as 0 to just popup number, not change anything
void InstrumentClipView::nudgeNotes(int32_t offset) {

	shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress = true;

	// If just popping up number, but multiple presses, we're quite limited with what intelligible stuff we can display
	if (!offset && numEditPadPresses > 1) {
		return;
	}

	int32_t resultingTotalOffset = 0;

	bool foundOne = false;
	int32_t xDisplay;

	// Declare these out here so we can keep the value - we'll use this for the mod region if there was only 1 press
	int32_t newPos;
	NoteRow* noteRow;
	int32_t noteRowId;

	bool didAnySuccessfulNudging = false;

	InstrumentClip* currentClip = getCurrentInstrumentClip();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// If the user is nudging back in the direction they just nudged, we can do a (possibly partial) undo, getting back the proper length of any notes that got trimmed etc.

	Action* lastAction = actionLogger.firstAction[BEFORE];
	if (offset && lastAction && lastAction->type == ACTION_NOTE_NUDGE && lastAction->openForAdditions
	    && lastAction->offset == -offset) {

		didAnySuccessfulNudging = true;

		actionLogger.undoJustOneConsequencePerNoteRow(modelStack);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(modelStack->song->currentClip);

		// Still have to work out resultingTotalOffset, to display for the user
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {

				int32_t noteRowIndex;
				noteRow = currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				noteRowId = currentClip->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				newPos = editPadPresses[i].intendedPos + offset;
				if (newPos < 0) {
					newPos += noteRowEffectiveLength;
				}
				else if (newPos >= noteRowEffectiveLength) {
					newPos -= noteRowEffectiveLength;
				}

				int32_t n = noteRow->notes.search(newPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(n);
				if (note && note->pos == newPos) {
					editPadPresses[i].intendedPos = newPos;
				}
				else {
					newPos = editPadPresses[i].intendedPos;
				}

				if (!foundOne) {
					foundOne = true;
					xDisplay = editPadPresses[i].xDisplay;
					int32_t squareStart = getPosFromSquare(xDisplay);
					resultingTotalOffset = editPadPresses[i].intendedPos - squareStart;
				}
			}
		}
	}

	// Or, if not doing the partial-undo method, we'll just try and do a plain old nudge
	else {
		Action* action = NULL;

		if (offset) {
			action = actionLogger.getNewAction(ACTION_NOTE_NUDGE, ACTION_ADDITION_ALLOWED);
			if (action) {
				action->offset = offset;
			}
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(modelStack->song->currentClip);

		// For each note / pad held down...
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (editPadPresses[i].isActive) {
				editPadPresses[i].deleteOnDepress = false;

				if (offset) {
					editPadPresses[i].isBlurredSquare = true; // So it doesn't get dragged along with a vertical scroll
				}

				int32_t noteRowIndex;
				noteRow = currentClip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
				noteRowId = currentClip->getNoteRowId(noteRow, noteRowIndex);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

				int32_t noteRowEffectiveLength = modelStackWithNoteRow->getLoopLength();

				newPos = editPadPresses[i].intendedPos + offset;
				if (newPos < 0) {
					newPos += noteRowEffectiveLength;
				}
				else if (newPos >= noteRowEffectiveLength) {
					newPos -= noteRowEffectiveLength;
				}

				bool gotCollision = false;

				if (offset) {
					// We're going to nudge notes across all screens, but before we do, check if this particular note is gonna collide with anything
					int32_t searchBoundary;
					int32_t searchDirection;
					int32_t n;
					if (offset >= 0) { // Nudging right
						if (newPos == 0) {
							n = 0;
							goto doCompareNote;
						}
						else {
							searchBoundary = newPos;
							searchDirection = GREATER_OR_EQUAL;
doSearch:
							n = noteRow->notes.search(searchBoundary, searchDirection);
doCompareNote:
							Note* note = noteRow->notes.getElement(n);
							if (note && note->pos == newPos) {
								newPos =
								    editPadPresses[i]
								        .intendedPos; // Make it so the below code just displays the already existing offset
								gotCollision = true;
							}
						}
					}
					else { // Nudging left
						if (editPadPresses[i].intendedPos == 0) {
							n = noteRow->notes.getNumElements();
							goto doCompareNote;
						}
						else {
							searchBoundary = editPadPresses[i].intendedPos;
							searchDirection = LESS;
							goto doSearch;
						}
					}
				}

				if (!foundOne) {
					foundOne = true;
					xDisplay = editPadPresses[i].xDisplay;
					int32_t squareStart = getPosFromSquare(xDisplay);
					resultingTotalOffset = newPos - squareStart;
					if (!offset) {
						break;
					}
				}

				if (!gotCollision) {
					int32_t distanceTilNext =
					    noteRow->getDistanceToNextNote(editPadPresses[i].intendedPos, modelStackWithNoteRow);

					int32_t error =
					    noteRow->nudgeNotesAcrossAllScreens(editPadPresses[i].intendedPos, modelStackWithNoteRow,
					                                        action, currentClip->getWrapEditLevel(), offset);
					if (error) {
						display->displayError(error);
						return;
					}

					// Nudge automation at NoteRow level, while our ModelStack still has a pointer to the NoteRow
					{
						ModelStackWithThreeMainThings* modelStackWithThreeMainThingsForNoteRow =
						    modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();
						noteRow->paramManager.nudgeAutomationHorizontallyAtPos(
						    editPadPresses[i].intendedPos, offset,
						    modelStackWithThreeMainThingsForNoteRow->getLoopLength(), action,
						    modelStackWithThreeMainThingsForNoteRow, distanceTilNext);
					}

					// WARNING! A bit dodgy, but at this stage, we can no longer refer to modelStackWithNoteRow, cos we're going to reuse its
					// parent ModelStackWithTimelineCounter, below.

					// Nudge automation at Clip level
					{
						int32_t lengthBeforeLoop = currentClip->getLoopLength();
						ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
						    modelStackWithTimelineCounter->addOtherTwoThingsButNoNoteRow(
						        currentClip->output->toModControllable(), &currentClip->paramManager);
						currentClip->paramManager.nudgeAutomationHorizontallyAtPos(editPadPresses[i].intendedPos,
						                                                           offset, lengthBeforeLoop, action,
						                                                           modelStackWithThreeMainThings);
					}

					editPadPresses[i].intendedPos = newPos;
					didAnySuccessfulNudging = true;
				}
				else {
					newPos = editPadPresses[i].intendedPos;
				}
			}
		}
	}

	// Now, decide what message to display ---------------------------------------------------
	char buffer[display->haveOLED() ? 24 : 5];
	char const* message;
	bool alignRight = false;

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(
	    modelStack->song
	        ->currentClip); // Can finally do this since we're not going to use the bare ModelStack for anything else

	if (numEditPadPresses > 1) {
		if (!didAnySuccessfulNudging) {
			return; // Don't want to see these "multiple pads moved" messages if in fact none were moved
		}
		if (display->haveOLED()) {
			message = (offset >= 0) ? "Nudged notes right" : "Nudged notes left";
		}
		else {
			message = (offset >= 0) ? "RIGHT" : "LEFT";
		}
	}

	else {
		if (resultingTotalOffset >= (currentClip->loopLength >> 1)) {
			resultingTotalOffset -= currentClip->loopLength;
		}
		else if (resultingTotalOffset <= -(currentClip->loopLength >> 1)) {
			resultingTotalOffset += currentClip->loopLength;
		}

		if (resultingTotalOffset > 0) {

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);

			int32_t squareWidth = getSquareWidth(xDisplay, modelStackWithNoteRow->getLoopLength());
			if (resultingTotalOffset > (squareWidth >> 1)) {
				if (!doneAnyNudgingSinceFirstEditPadPress) {
					offsettingNudgeNumberDisplay = true;
				}
			}
			else {
				offsettingNudgeNumberDisplay = false;
			}

			if (offsettingNudgeNumberDisplay) {
				resultingTotalOffset -= squareWidth;
			}
		}

		if (display->haveOLED()) {
			strcpy(buffer, "Note nudge: ");
			intToString(resultingTotalOffset, buffer + strlen(buffer));
			message = buffer;
		}
		else {
			if (resultingTotalOffset > 9999) {
				message = "RIGHT";
			}
			else if (resultingTotalOffset < -999) {
				message = "LEFT";
			}
			else {
				message = buffer;
				alignRight = true;
				intToString(resultingTotalOffset, buffer);
			}
		}
	}

	if (display->haveOLED()) {
		display->popupTextTemporary(message);
	}
	else {
		display->displayPopup(message, 0, alignRight);
	}

	doneAnyNudgingSinceFirstEditPadPress =
	    true; // Even if we didn't actually nudge, we want to record this for the purpose of the offsetting of the number display - see above

	if (!offset) {
		return;
	}

	// If multiple presses, just abandon the mod region
	if (numEditPadPresses > 1) {
abandonModRegion:
		view.setModRegion();
	}

	// Otherwise, update it for what they actually intend
	else {

		int32_t i = noteRow->notes.search(newPos, GREATER_OR_EQUAL);
		Note* note = noteRow->notes.getElement(i);
		if (!note || note->pos != newPos) {
			goto abandonModRegion;
		}

		// Edit mod knob values for this Note's region
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(noteRowId, noteRow);
		int32_t distanceToNextNote = currentClip->getDistanceToNextNote(note, modelStackWithNoteRow);
		//view.setModRegion(newPos, max((uint32_t)distanceToNextNote + lastNote->pos - firstNote->pos, squareWidth)); // This is what happens with initial press, kinda different...
		view.setModRegion(newPos, distanceToNextNote, modelStackWithNoteRow->noteRowId);
	}

	uiNeedsRendering(this, 0xFFFFFFFF, 0);

	if (playbackHandler.isEitherClockActive() && modelStackWithTimelineCounter->song->isClipActive(currentClip)) {
		currentClip->expectEvent();
		currentClip->reGetParameterAutomation(modelStackWithTimelineCounter);
	}
}
#pragma GCC pop

void InstrumentClipView::graphicsRoutine() {
	if (!currentSong) {
		return; // Briefly, if loading a song fails, during the creation of a new blank one, this could happen.
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return;
	}

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) {
		return;
	}

	int32_t newTickSquare;

	bool reallyNoTickSquare = (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)
	                           || currentUIMode == UI_MODE_EXPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn);

	if (reallyNoTickSquare) {
		newTickSquare = 255;
	}
	else {
		newTickSquare = getTickSquare();
		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, newTickSquare, kDisplayHeight);

	uint8_t colours[kDisplayHeight];
	uint8_t nonMutedColour = clip->getCurrentlyRecordingLinearly() ? 2 : 0;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t noteRowIndex;
		NoteRow* noteRow = clip->getNoteRowOnScreen(yDisplay, currentSong, &noteRowIndex);
		colours[yDisplay] = (noteRow && noteRow->muted) ? 1 : nonMutedColour;

		if (!reallyNoTickSquare) {
			if (noteRow && noteRow->hasIndependentPlayPos()) {

				int32_t noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);

				int32_t rowTickSquare = getSquareFromPos(noteRow->getLivePos(modelStackWithNoteRow));
				if (rowTickSquare < 0 || rowTickSquare >= kDisplayWidth) {
					rowTickSquare = 255;
				}
				tickSquares[yDisplay] = rowTickSquare;
			}
		}
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

void InstrumentClipView::fillOffScreenImageStores() {

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_CLIP];
	uint32_t xScroll = currentSong->xScroll[NAVIGATION_CLIP];

	// We're also going to fill up an extra, currently-offscreen imageStore row, with all notes currently offscreen

	int32_t noteRowIndexBottom, noteRowIndexTop;
	if (getCurrentOutputType() == OutputType::KIT) {
		noteRowIndexBottom = getCurrentInstrumentClip()->yScroll;
		noteRowIndexTop = getCurrentInstrumentClip()->yScroll + kDisplayHeight;
	}
	else {
		noteRowIndexBottom = getCurrentInstrumentClip()->noteRows.search(
		    getCurrentInstrumentClip()->getYNoteFromYDisplay(0, currentSong), GREATER_OR_EQUAL);
		noteRowIndexTop = getCurrentInstrumentClip()->noteRows.search(
		    getCurrentInstrumentClip()->getYNoteFromYDisplay(kDisplayHeight, currentSong), GREATER_OR_EQUAL);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	getCurrentClip()->renderAsSingleRow(modelStack, this, xScroll, xZoom, PadLEDs::imageStore[0],
	                                    PadLEDs::occupancyMaskStore[0], false, 0, noteRowIndexBottom, 0, kDisplayWidth,
	                                    true, false);
	getCurrentClip()->renderAsSingleRow(modelStack, this, xScroll, xZoom, PadLEDs::imageStore[kDisplayHeight],
	                                    PadLEDs::occupancyMaskStore[kDisplayHeight], false, noteRowIndexTop, 2147483647,
	                                    0, kDisplayWidth, true, false);

	// Clear sidebar pads from offscreen image stores
	for (int32_t x = kDisplayWidth; x < kDisplayWidth + kSideBarWidth; x++) {
		for (int32_t colour = 0; colour < 3; colour++) {
			PadLEDs::imageStore[0][x][colour] = 0;
			PadLEDs::imageStore[kDisplayHeight][x][colour] = 0;
		}
		PadLEDs::occupancyMaskStore[0][x] = 0;
		PadLEDs::occupancyMaskStore[kDisplayHeight][x] = 0;
	}
}

uint32_t InstrumentClipView::getSquareWidth(int32_t square, int32_t effectiveLength) {
	int32_t squareRightEdge = getPosFromSquare(square + 1);
	return std::min(effectiveLength, squareRightEdge) - getPosFromSquare(square);
}

void InstrumentClipView::flashDefaultRootNote() {
	flashDefaultRootNoteOn = !flashDefaultRootNoteOn;
	uiNeedsRendering(this, 0, 0xFFFFFFFF);
	uiTimerManager.setTimer(TIMER_DEFAULT_ROOT_NOTE, kFlashTime);
}

void InstrumentClipView::noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) {

	if (currentUIMode & UI_MODE_HORIZONTAL_SCROLL) {
		return;
	}

	if (clip == getCurrentInstrumentClip()) {
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong)) {
				uiNeedsRendering(this, 1 << yDisplay, 0);
			}
		}
	}
}

bool InstrumentClipView::isDrumAuditioned(Drum* drum) {

	if (getCurrentOutputType() != OutputType::KIT) {
		return false;
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (auditionPadIsPressed[yDisplay]) {
			NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);
			if (noteRow && noteRow->drum == drum) {
				return true;
			}
		}
	}

	return false;
}

bool InstrumentClipView::getAffectEntire() {
	return getCurrentInstrumentClip()->affectEntire;
}

void InstrumentClipView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowOnScreen(yDisplay, currentSong);
		PadLEDs::transitionTakingPlaceOnRow[yDisplay] = (noteRow && !noteRow->hasNoNotes());
	}
}

void InstrumentClipView::notifyPlaybackBegun() {
	reassessAllAuditionStatus();
}

bool InstrumentClipView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                        bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	PadLEDs::renderingLock = true;
	performActualRender(whichRows, image[0], occupancyMask, currentSong->xScroll[NAVIGATION_CLIP],
	                    currentSong->xZoom[NAVIGATION_CLIP], kDisplayWidth, kDisplayWidth + kSideBarWidth,
	                    drawUndefinedArea);
	PadLEDs::renderingLock = false;

	return true;
}

// occupancyMask now optional
void InstrumentClipView::performActualRender(uint32_t whichRows, RGB* image,
                                             uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                             uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
                                             bool drawUndefinedArea) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		if (whichRows & (1 << yDisplay)) {

			ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(yDisplay, modelStack);

			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			uint8_t* occupancyMaskOfRow = NULL;
			if (occupancyMask) {
				occupancyMaskOfRow = occupancyMask[yDisplay];
			}

			// If row doesn't have a NoteRow, wipe it empty
			if (!noteRow) {
				std::fill(image, &image[renderWidth], colours::black);
				if (occupancyMask) {
					memset(occupancyMaskOfRow, 0, renderWidth);
				}
			}

			// Otherwise render the row
			else {
				noteRow->renderRow(this, rowColour[yDisplay], rowTailColour[yDisplay], rowBlurColour[yDisplay], image,
				                   occupancyMaskOfRow, true, modelStackWithNoteRow->getLoopLength(),
				                   clip->allowNoteTails(modelStackWithNoteRow), renderWidth, xScroll, xZoom, 0,
				                   renderWidth, false);
			}

			if (drawUndefinedArea) {
				int32_t effectiveLength = modelStackWithNoteRow->getLoopLength();

				clip->drawUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMaskOfRow, renderWidth, this,
				                        currentSong->tripletsOn); // Sends image pointer for just the one row
			}
		}

		image += imageWidth;
	}
}

void InstrumentClipView::playbackEnded() {

	// Easter egg - if user's holding down a note, we want it to be edit-auditioned again now
	reassessAllAuditionStatus();
}

void InstrumentClipView::scrollFinished() {
	if (currentUIMode == UI_MODE_AUDITIONING) {
		uiNeedsRendering(
		    this, 0xFFFFFFFF,
		    0); // Needed because sometimes we initiate a scroll before reverting an Action, so we need to properly render again afterwards
	}

	else {
		ClipView::scrollFinished();
	}
}

void InstrumentClipView::clipNeedsReRendering(Clip* clip) {
	if (clip == getCurrentInstrumentClip()) {
		uiNeedsRendering(this); // Re-renders sidebar too. Probably a good idea? Can't hurt?
	}
}

void InstrumentClipView::dontDeleteNotesOnDepress() {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		editPadPresses[i].deleteOnDepress = false;
	}
}

void InstrumentClipView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	dontDeleteNotesOnDepress();

	InstrumentClip* clip = getCurrentInstrumentClip();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	Output* output = clip->output;

	if (output->type == OutputType::KIT && isUIModeActive(UI_MODE_AUDITIONING)) {

		Kit* kit = (Kit*)output;

		if (kit->selectedDrum && kit->selectedDrum->type != DrumType::SOUND) {

			if (ALPHA_OR_BETA_VERSION && !kit->activeClip) {
				FREEZE_WITH_ERROR("E381");
			}

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(kit->activeClip);
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    ((InstrumentClip*)kit->activeClip)
			        ->getNoteRowForDrum(modelStackWithTimelineCounter,
			                            kit->selectedDrum); // The NoteRow probably doesn't get referred to...

			NonAudioDrum* drum = (NonAudioDrum*)kit->selectedDrum;

			ParamManagerForTimeline* paramManager = NULL;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
			if (noteRow) {
				paramManager = &noteRow->paramManager; // Should be NULL currently, cos it's a NonAudioDrum.
			}
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStackWithNoteRow->addOtherTwoThings(drum->toModControllable(), paramManager);

			drum->modEncoderAction(modelStackWithThreeMainThings, offset, whichModEncoder);
		}
	}

	ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}

// Check UI mode is appropriate before calling this
void InstrumentClipView::editNumEuclideanEvents(ModelStackWithNoteRow* modelStack, int32_t offset, int32_t yDisplay) {

	int32_t newNumNotes = 0;

	int32_t effectiveLength = modelStack->getLoopLength();

	uint32_t squareWidth = getSquareWidth(0, kMaxSequenceLength);
	int32_t numStepsAvailable = (uint32_t)(effectiveLength - 1) / squareWidth + 1; // Round up

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		if (!offset) {
			goto displayNewNumNotes;
		}
		return;
	}

	{
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

		int32_t oldNumNotes = noteRow->notes.getNumElements();
		newNumNotes = oldNumNotes;

		if (offset) { // Or if offset is 0, we'll just display the current number, below, without changing anything
			newNumNotes += offset;
			if (newNumNotes < 0) { // If can't go lower, just display old number
justDisplayOldNumNotes:
				newNumNotes = oldNumNotes;
				goto displayNewNumNotes;
			}

			// If there aren't enough steps...
			if (newNumNotes > numStepsAvailable) {
				// If user was trying to increase num events, well they just can't
				if (offset >= 0) {
					goto justDisplayOldNumNotes;

					// Or if they're decreasing, well decrease further
				}
				else {
					newNumNotes = numStepsAvailable;
				}
			}

			// Do a "partial undo" if we can
			Action* lastAction = actionLogger.firstAction[BEFORE];
			// No need to check that lastAction was for the same Clip or anything - the Action gets "closed" manually when we stop auditioning.
			if (lastAction && lastAction->type == ACTION_EUCLIDEAN_NUM_EVENTS_EDIT && lastAction->openForAdditions
			    && lastAction->offset == -offset) {

				char modelStackMemory2[MODEL_STACK_MAX_SIZE];
				ModelStack* modelStackWithJustSong = setupModelStackWithSong(modelStackMemory2, modelStack->song);

				bool revertedWholeAction = actionLogger.undoJustOneConsequencePerNoteRow(modelStackWithJustSong);
				if (!revertedWholeAction) {
					goto noteRowChanged;
				}
			}

			else {
				{
					// Make new NoteVector for the new Notes, since ActionLogger should be "stealing" the old data
					NoteVector newNotes;
					if (newNumNotes) {
						int32_t error = newNotes.insertAtIndex(0, newNumNotes); // Pre-allocate, so no errors later
						if (error) {
							display->displayError(error);
							return;
						}
					}

					// Record Action
					Action* action =
					    actionLogger.getNewAction(ACTION_EUCLIDEAN_NUM_EVENTS_EDIT, ACTION_ADDITION_ALLOWED);
					if (action) {
						action->offset = offset;
					}

					// Create the Notes
					for (int32_t n = 0; n < newNumNotes; n++) {
						Note* note = newNotes.getElement(n);
						note->pos = (uint32_t)(n * numStepsAvailable) / (uint32_t)newNumNotes * squareWidth;
						note->length = squareWidth;
						note->probability = noteRow->getDefaultProbability(modelStack);
						note->velocity = ((Instrument*)clip->output)->defaultVelocity;
						note->lift = kDefaultLiftValue;
					}

					// Just make sure final note isn't too long
					if (newNumNotes) {
						Note* note = newNotes.getElement(newNumNotes - 1);
						int32_t maxLength = effectiveLength - note->pos;
						if (note->length > maxLength) {
							note->length = maxLength;
						}
					}

					// Delete / steal / consequence-ize the MPE data first, because in order for partial undos to work, this has to be further down the
					// linked list of Consequences than the note-array-change that we do next, below.
					ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
					ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
					if (mpeParams) {
						ModelStackWithParamCollection* modelStackWithParamCollection =
						    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(
						        mpeParams, mpeParamsSummary);
						mpeParams->deleteAllAutomation(action, modelStackWithParamCollection);
					}

					// Record change, stealing the old note data
					if (action) {
						// We "definitely" store the change, because unusually, we may want to revert individual Consequences in the Action one by one
						action->recordNoteArrayChangeDefinitely(clip, modelStack->noteRowId, &noteRow->notes, true);
					}

					// Swap the new temporary note data into the permanent place
					noteRow->notes.swapStateWith(&newNotes);

#if ENABLE_SEQUENTIALITY_TESTS
					noteRow->notes.testSequentiality("E376");
#endif
				}

noteRowChanged:
				// Play it
				clip->expectEvent();

				// Render it
				if (yDisplay >= 0 && yDisplay < kDisplayHeight) {
					uiNeedsRendering(this, 1 << yDisplay, 0);
				}
			}
		}
	}
displayNewNumNotes:
	// Tell the user about it in text
	if (display->haveOLED()) {
		char buffer[34];
		strcpy(buffer, "Events: ");
		char* pos = strchr(buffer, 0);
		intToString(newNumNotes, pos);
		pos = strchr(buffer, 0);
		strcpy(pos, " of ");
		pos = strchr(buffer, 0);
		intToString(numStepsAvailable, pos);
		display->popupTextTemporary(buffer);
	}
	else {
		char buffer[12];
		intToString(newNumNotes, buffer);
		display->displayPopup(buffer, 0, true);
	}
}

// Check UI mode is appropriate before calling this
void InstrumentClipView::rotateNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int32_t offset, int32_t yDisplay,
                                                   bool shouldDisplayDirectionEvenIfNoNoteRow) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		if (shouldDisplayDirectionEvenIfNoNoteRow) {
			goto displayMessage;
		}
		return;
	}

	{
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

		uint32_t squareWidth = getSquareWidth(0, kMaxSequenceLength);
		int32_t shiftAmount = offset * squareWidth;

		clip->shiftOnlyOneNoteRowHorizontally(modelStack, shiftAmount);

		// Render change
		if (yDisplay >= 0 && yDisplay < kDisplayHeight) {
			uiNeedsRendering(this, 1 << yDisplay, 0);
		}

		// If possible, just modify a previous Action to add this new shift amount to it.
		Action* action = actionLogger.firstAction[BEFORE];
		if (action && action->type == ACTION_NOTEROW_HORIZONTAL_SHIFT && action->openForAdditions
		    && action->currentClip == clip) {

			// If there's no Consequence in the Action, that's probably because we deleted it a previous time with the code just below.
			// Or possibly because the Action was created but there wasn't enough RAM to create the Consequence. Anyway, just go add a consequence now.
			if (!action->firstConsequence) {
				goto addConsequenceToAction;
			}

			ConsequenceNoteRowHorizontalShift* consequence =
			    (ConsequenceNoteRowHorizontalShift*)action->firstConsequence;
			if (consequence->noteRowId != modelStack->noteRowId) {
				goto getNewAction;
			}

			consequence->amount += shiftAmount;
		}

		// Or if no previous Action, go create a new one now.
		else {
getNewAction:
			action = actionLogger.getNewAction(ACTION_NOTEROW_HORIZONTAL_SHIFT, ACTION_ADDITION_NOT_ALLOWED);
			if (action) {
addConsequenceToAction:
				void* consMemory =
				    GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteRowHorizontalShift));

				if (consMemory) {
					ConsequenceNoteRowHorizontalShift* newConsequence =
					    new (consMemory) ConsequenceNoteRowHorizontalShift(modelStack->noteRowId, shiftAmount);
					action->addConsequence(newConsequence);
				}
			}
		}
	}

displayMessage:
	if (display->haveOLED()) {
		char const* message = (offset == 1) ? "Rotated right" : "Rotated left";
		display->popupTextTemporary(message);
	}
	else {
		char const* message = (offset == 1) ? "RIGHT" : "LEFT";
		display->displayPopup(message, 0);
	}
}

extern bool shouldResumePlaybackOnNoteRowLengthSet;

// Check UI mode is appropriate before calling this.
// Can handle being given a NULL NoteRow, in which case it'll do nothing.
void InstrumentClipView::editNoteRowLength(ModelStackWithNoteRow* modelStack, int32_t offset, int32_t yDisplay) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow) {
		return;
	}
	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int32_t oldLength = modelStack->getLoopLength();

	// If we're not scrolled all the way to the right, go there now. If we were already further right than the end of this NoteRow, it's ok, we'll stay there.
	if (scrollRightToEndOfLengthIfNecessary(oldLength)) {
		return; // ActionResult::DEALT_WITH;
	}

	uint32_t squareWidth = getSquareWidth(0, kMaxSequenceLength);

	int32_t oldNumSteps = (uint32_t)(oldLength - 1) / squareWidth + 1; // Round up
	int32_t newNumSteps = oldNumSteps + offset;
	if (newNumSteps <= 0) {
		return;
	}
	int32_t newLength = newNumSteps * squareWidth;
	if (newLength > kMaxSequenceLength) {
		return;
	}

	int32_t oldPos =
	    modelStack
	        ->getLastProcessedPos(); // We have to grab and remember this before an initial revert() ("secret UNDO") potentially stuffs it up
	bool hadIndependentPlayPosBefore = noteRow->hasIndependentPlayPos();
	bool didSecretUndo = false;

	// See if we can do a secret undo
	Action* prevAction = actionLogger.firstAction[BEFORE];
	if (prevAction && prevAction->openForAdditions && prevAction->type == ACTION_NOTEROW_LENGTH_EDIT
	    && prevAction->currentClip == clip) {

		ConsequenceNoteRowLength* prevCons = (ConsequenceNoteRowLength*)prevAction->firstConsequence;
		if (prevCons->noteRowId != modelStack->noteRowId) {
			goto editLengthWithNewAction;
		}

		// If we're recovering a bit that previously got chopped off, do secret undo to recover any chopped-off notes and automation
		if (offset == 1 && prevCons->backedUpLength > oldLength) {
			shouldResumePlaybackOnNoteRowLengthSet = false; // Ugly hack, kinda
			actionLogger.revert(BEFORE, false, false);
			shouldResumePlaybackOnNoteRowLengthSet = true;
			didSecretUndo = true;

			// If that got us to the intended length, all is good...
			if (noteRow->loopLengthIfIndependent == newLength
			    || (!noteRow->loopLengthIfIndependent && clip->loopLength == newLength)) {
possiblyDoResumePlaybackOnNoteRow:
				// Need to do the resumePlayback that we blocked happening during the revert()
				if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {
					noteRow->resumePlayback(modelStack, true);
				}
			}

			// Otherwise, go make a new Action and get to correct length
			else {
				goto editLengthWithNewAction;
			}
		}

		// Otherwise, the Action / Consequence is still fine for doing a future UNDO, so just edit length as needed.
		// But we'll still pass the prevAction in, so that anything which wasn't snapshotted yet (because no Notes happened to get trimmed last time)
		// can get snapshotted now.
		else {
			noteRow->setLength(modelStack, newLength, prevAction, oldPos,
			                   hadIndependentPlayPosBefore); // Might call resumePlayback() too.
		}
	}

	else {
editLengthWithNewAction:
		Action* action = actionLogger.getNewAction(ACTION_NOTEROW_LENGTH_EDIT, false);
		if (!action) {
ramError:
			display->displayError(ERROR_INSUFFICIENT_RAM);
			if (didSecretUndo) {
				// Need to do the resumePlayback that we blocked happening during the revert()
				if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {
					noteRow->resumePlayback(modelStack, true);
				}
			}
			return;
		}

		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteRowLength));
		if (!consMemory) {
			goto ramError;
		}

		ConsequenceNoteRowLength* newConsequence =
		    new (consMemory) ConsequenceNoteRowLength(modelStack->noteRowId, newLength);
		action->addConsequence(newConsequence);

		// The ConsequenceNoteRowLength does the actual work for us for this function
		newConsequence->performChange(modelStack, action, oldPos, hadIndependentPlayPosBefore);
	}

	bool didScroll;

	// Lengthening
	if (offset == 1) {
		didScroll = scrollRightToEndOfLengthIfNecessary(newLength);
		if (!didScroll) {
			goto tryScrollingLeft;
		}
	}

	// Shortening
	else {
tryScrollingLeft:
		didScroll = scrollLeftIfTooFarRight(newLength);
	}

	if (display->haveOLED()) {
		char buffer[19];
		strcpy(buffer, "Steps: ");
		intToString(newNumSteps, buffer + strlen(buffer));
		display->popupTextTemporary(buffer);
	}
	else {
		char buffer[12];
		intToString(newNumSteps, buffer);
		display->displayPopup(buffer, 0, true);
	}

	// Play it
	clip->expectEvent();

	// Render it
	if (!didScroll && yDisplay >= 0 && yDisplay < kDisplayHeight) {
		uiNeedsRendering(this, 1 << yDisplay, 0);
	}
}

void InstrumentClipView::reportMPEInitialValuesForNoteEditing(ModelStackWithNoteRow* modelStack,
                                                              int16_t const* mpeValues) {

	NoteRow* noteRow = modelStack->getNoteRowAllowNull();

	// MPE stuff - if editing note, we need to take note of the initial values which might have been sent before this note-on.
	if (noteRow && view.modLength && modelStack->noteRowId == view.modNoteRowId
	    && modelStack->getTimelineCounter() == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

		noteRow->paramManager.ensureExpressionParamSetExists(); // If this fails, we'll detect that below.

		ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
		ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;

		if (mpeParams) {

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams,
			                                                                                 mpeParamsSummary);

			for (int32_t whichExpressionDimension = 0; whichExpressionDimension < kNumExpressionDimensions;
			     whichExpressionDimension++) {
				mpeValuesAtHighestPressure[0][whichExpressionDimension] = mpeValues[whichExpressionDimension];
			}
		}
	}
}

void InstrumentClipView::reportMPEValueForNoteEditing(int32_t whichExpressionimension, int32_t value) {

	// If time to move record along...
	uint32_t timeSince = AudioEngine::audioSampleTimer - mpeRecordLastUpdateTime;
	if (timeSince >= MPE_RECORD_INTERVAL_TIME) {
		mpeRecordLastUpdateTime += MPE_RECORD_INTERVAL_TIME;
		memmove(mpeValuesAtHighestPressure[1], mpeValuesAtHighestPressure[0],
		        sizeof(uint16_t) * kNumExpressionDimensions * (MPE_RECORD_LENGTH_FOR_NOTE_EDITING - 1));
		mpeValuesAtHighestPressure[0][2] = 0; // Yes only reset the "pressure" of the new/first record
		mpeMostRecentPressure = 0;
	}

	// Always keep track of the "current" pressure value, so we can decide whether to be recording the other values.
	if (whichExpressionimension == 2) {
		mpeMostRecentPressure = value >> 16;
	}

	// And if we're still at max pressure, then yeah, record those other values.
	if (mpeMostRecentPressure >= mpeValuesAtHighestPressure[0][2]) {
		mpeValuesAtHighestPressure[0][whichExpressionimension] = value >> 16;
	}

	dontDeleteNotesOnDepress(); // We know the caller is also manually editing the AutoParam now too - this counts as an edit, so we don't want the note deleted on press-release.
}

void InstrumentClipView::reportNoteOffForMPEEditing(ModelStackWithNoteRow* modelStack) {

	NoteRow* noteRow = modelStack->getNoteRow();

	// MPE stuff for note off - if they're still "editing" a note, they'll want the values from half a second ago, or the values from when they pressed hardest.
	if (view.modLength && modelStack->noteRowId == view.modNoteRowId
	    && modelStack->getTimelineCounter() == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

		ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
		ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
		if (!mpeParams) {
			return;
		}

		int32_t t = MPE_RECORD_LENGTH_FOR_NOTE_EDITING - 1;
		while (mpeValuesAtHighestPressure[t][2] == -1) {
			if (!t) {
				return; // No data has been recorded
			}
			t--;
		}

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams, mpeParamsSummary);

		for (int32_t whichExpressionDimension = 0; whichExpressionDimension < kNumExpressionDimensions;
		     whichExpressionDimension++) {
			AutoParam* param = &mpeParams->params[whichExpressionDimension];

			ModelStackWithAutoParam* modelStackWithAutoParam =
			    modelStackWithParamCollection->addAutoParam(whichExpressionDimension, param);

			int32_t newValue = (int32_t)mpeValuesAtHighestPressure[t][whichExpressionDimension] << 16;

			param->setValueForRegion(view.modPos, view.modLength, newValue, modelStackWithAutoParam);
		}

		dontDeleteNotesOnDepress();
	}
}
