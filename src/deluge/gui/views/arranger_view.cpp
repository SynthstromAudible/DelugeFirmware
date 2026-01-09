/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/views/arranger_view.h"
#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/stem_export/cancel_stem_export.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/rename/rename_output_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/visualizer.h"
#include "hid/encoder.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/consequence/consequence_arranger_params_time_inserted.h"
#include "model/consequence/consequence_clip_existence.h"
#include "model/consequence/consequence_clip_instance_change.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include "util/try.h"
#include <algorithm>
#include <cstdint>
#include <new>

extern "C" {
extern uint8_t currentlyAccessingCard;
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

PLACE_SDRAM_BSS ArrangerView arrangerView{};

void ArrangerView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (stemExport.processStarted) {
		if (stemExport.exportMixdown) {
			stemExport.displayStemExportProgressOLED(StemExportType::MIXDOWN);
		}
		else {
			stemExport.displayStemExportProgressOLED(StemExportType::TRACK);
		}
		return;
	}

	// Check if visualizer should be displayed
	if (deluge::hid::display::Visualizer::potentiallyRenderVisualizer(canvas)) {
		return;
	}

	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		Output* output = outputsOnScreen[yPressedEffective];
		view.displayOutputName(output);
	}
	else {
		sessionView.renderOLED(canvas);
	}
}

void ArrangerView::moveClipToSession() {
	Output* output = outputsOnScreen[yPressedEffective];
	ClipInstance* clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);
	Clip* clip = clipInstance->clip;

	// Empty ClipInstance - can't do
	if (!clip) {
		display->displayPopup(
		    deluge::l10n::get(deluge::l10n::String::STRING_FOR_EMPTY_CLIP_INSTANCES_CANT_BE_MOVED_TO_THE_SESSION));
	}

	else {
		// Clip already exists in session - just go to it
		if (!clip->isArrangementOnlyClip()) {
			int32_t index = currentSong->sessionClips.getIndexForClip(clip);
			currentSong->songViewYScroll = index - yPressedEffective;
		}

		// Or, arrangement-only Clip needs moving to session
		else {
			int32_t intendedIndex = currentSong->songViewYScroll + yPressedEffective;

			if (intendedIndex < 0) {
				currentSong->songViewYScroll -= intendedIndex;
				intendedIndex = 0;
			}

			else if (intendedIndex > currentSong->sessionClips.getNumElements()) {
				currentSong->songViewYScroll -= intendedIndex - currentSong->sessionClips.getNumElements();
				intendedIndex = currentSong->sessionClips.getNumElements();
			}

			clip->section = currentSong->getLowestSectionWithNoSessionClipForOutput(output);
			Error error = currentSong->sessionClips.insertClipAtIndex(clip, intendedIndex);
			if (error != Error::NONE) {
				display->displayError(error);
				return;
			}
			actionLogger.deleteAllLogs();

			int32_t oldIndex = currentSong->arrangementOnlyClips.getIndexForClip(clip);
			if (oldIndex != -1) {
				currentSong->arrangementOnlyClips.deleteAtIndex(oldIndex);
			}
		}

		goToSongView();

		if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeRows) {
			currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
			sessionView.selectedClipYDisplay = yPressedEffective;
			sessionView.selectedClipPressYDisplay = yPressedActual;
			sessionView.selectedClipPressXDisplay = xPressed;
		}
		else {
			currentUIMode = UI_MODE_NONE;
		}

		sessionView.performActionOnPadRelease = false;
		view.setActiveModControllableTimelineCounter(clip);
	}
}

void ArrangerView::goToSongView() {
	currentSong->xScroll[NAVIGATION_CLIP] = currentSong->xScrollForReturnToSongView;
	currentSong->xZoom[NAVIGATION_CLIP] = currentSong->xZoomForReturnToSongView;
	changeRootUI(&sessionView);
}

ActionResult ArrangerView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// when stem export process has started,
	// do not action anybutton presses except BACK to cancel the process
	if (b != BACK && stemExport.processStarted) {
		return ActionResult::DEALT_WITH;
	}

	OutputType newOutputType;

	// Song button
	if (b == SESSION_VIEW) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (currentUIMode == UI_MODE_NONE) {
				if (Buttons::isShiftButtonPressed()) {
					automationView.onArrangerView = true;
					changeRootUI(&automationView);
				}
				else {
					goToSongView();
				}
			}
			else if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW) {
				moveClipToSession();
			}
		}
	}

	// Affect-entire button
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			currentSong->affectEntire = !currentSong->affectEntire;
			// setLedStates();
			view.setActiveModControllableTimelineCounter(currentSong);
		}
	}

	// Cross-screen button
	else if (b == CROSS_SCREEN_EDIT) {
		if (!on && currentUIMode == UI_MODE_NONE) {
			// if another button wasn't pressed while cross screen was held
			if (Buttons::considerCrossScreenReleaseForCrossScreenMode) {
				currentSong->arrangerAutoScrollModeActive = !currentSong->arrangerAutoScrollModeActive;
				indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

				if (currentSong->arrangerAutoScrollModeActive) {
					reassessWhetherDoingAutoScroll();
				}
				else {
					doingAutoScrollNow = false;
				}
			}
		}
	}

	// Record button - adds to what MatrixDriver does with it
	else if (b == RECORD) {
		if (on) {
			// trigger stem export when pressing record while holding save
			if (isUIModeActive(UI_MODE_HOLDING_SAVE_BUTTON)) {
				if (playbackHandler.isEitherClockActive() || playbackHandler.recording != RecordingMode::OFF) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EXPORT_STEMS));
				}
				else {
					if (inCardRoutine) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}
					if (stemExport.exportMixdown) {
						stemExport.startStemExportProcess(StemExportType::MIXDOWN);
					}
					else {
						stemExport.startStemExportProcess(StemExportType::TRACK);
					}
					return ActionResult::DEALT_WITH;
				}
			}

			else {
				uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
				blinkOn = true;
			}
		}
		else {
			if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
				currentUIMode = UI_MODE_NONE;
				PadLEDs::reassessGreyout(false);
				uiNeedsRendering(this, 0, 0xFFFFFFFF);
			}
		}
		return ActionResult::NOT_DEALT_WITH; // Make the MatrixDriver do its normal thing with it too
	}

	// cancel stem export process
	else if (b == BACK && isUIModeActive(UI_MODE_STEM_EXPORT)) {
		if (on) {
			bool available = context_menu::cancelStemExport.setupAndCheckAvailability();

			if (available) {
				display->setNextTransitionDirection(1);
				openUI(&context_menu::cancelStemExport);
			}
		}
	}

	// Save/delete button with row held
	else if (b == SAVE
	         && (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION
	             || currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW)) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (on) {
			deleteOutput();
		}
	}

	// Select encoder button
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeOutputToAudio();
		}
		// open Song FX menu
		else if (on && currentUIMode == UI_MODE_NONE) {
			display->setNextTransitionDirection(1);
			soundEditor.setup();
			openUI(&soundEditor);
		}
	}

	// Which-instrument-type buttons
	else if (b == SYNTH) {
		newOutputType = OutputType::SYNTH;

doChangeOutputType:
		if (on && currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION && !Buttons::isShiftButtonPressed()) {

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			Output* output = outputsOnScreen[yPressedEffective];

			// Don't allow converting audio output to instrument
			if (output->type == OutputType::AUDIO) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_CONVERT_TYPE));
			}

			// Instruments - just change type
			else {
				// don't allow clip type change if any clip instances belong to this
				// output are not empty
				// only impose this restriction if switching to/from kit clip
				if (((output->type == OutputType::KIT) || (newOutputType == OutputType::KIT)) && !output->isEmpty()) {
					return ActionResult::DEALT_WITH;
				}

				// If load button held, go into LoadInstrumentPresetUI
				if (Buttons::isButtonPressed(deluge::hid::button::LOAD)) {

					// Can't do that for MIDI or CV tracks though
					if (newOutputType == OutputType::MIDI_OUT || newOutputType == OutputType::CV) {
						goto doActualSimpleChange;
					}

					if (!output) {
						return ActionResult::DEALT_WITH;
					}

					actionLogger.deleteAllLogs();

					currentUIMode = UI_MODE_NONE;
					endAudition(output);

					loadInstrumentPresetUI.setupLoadInstrument(newOutputType, (Instrument*)output, nullptr);
					openUI(&loadInstrumentPresetUI);
				}

				// Otherwise, just change the instrument type
				else {
doActualSimpleChange:
					changeOutputType(newOutputType);
				}
			}
		}
	}

	else if (b == KIT) {
		newOutputType = OutputType::KIT;
		goto doChangeOutputType;
	}

	else if (b == MIDI) {
		newOutputType = OutputType::MIDI_OUT;
		goto doChangeOutputType;
	}

	else if (b == CV) {
		newOutputType = OutputType::CV;
		goto doChangeOutputType;
	}

	// Back button with <> button held
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			clearArrangement();
		}
	}

	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			performanceView.timeKeyboardShortcutPress = AudioEngine::audioSampleTimer;
			changeRootUI(&performanceView);
		}
	}

	else if (b == Y_ENC) {
		if (on && !Buttons::isShiftButtonPressed()) {
			UI* currentUI = getCurrentUI();
			bool isOLEDSessionView = display->haveOLED() && (currentUI == &sessionView || currentUI == &arrangerView);
			// only display pop-up if we're using 7SEG or we're not currently in Song / Arranger View
			if (!isOLEDSessionView) {
				currentSong->displayCurrentRootNoteAndScaleName();
			}
		}
	}

	else {
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

void ArrangerView::deleteOutput() {

	Output* output = outputsOnScreen[yPressedEffective];
	if (!output) {
		return;
	}

	char const* errorMessage;

	if (currentSong->getNumOutputs() <= 1) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_DELETE_FINAL_CLIP));
		return;
	}

	for (int32_t i = 0; i < output->clipInstances.getNumElements(); i++) {
		if (output->clipInstances.getElement(i)->clip) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_DELETE_ALL_TRACKS_CLIPS_FIRST));
			return;
		}
	}

	if (currentSong->getSessionClipWithOutput(output)) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_TRACK_STILL_HAS_CLIPS_IN_SESSION));
		return;
	}

	output->clipInstances.empty(); // Because none of these have Clips, this is ok
	output->cutAllSound();
	currentSong->deleteOrHibernateOutput(output);

	auditionEnded();

	repopulateOutputsOnScreen(true);
}

void ArrangerView::clearArrangement() {

	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ARRANGEMENT_CLEARED));

	if (arrangement.hasPlaybackActive()) {
		playbackHandler.endPlayback();
	}

	Action* action = actionLogger.getNewAction(ActionType::ARRANGEMENT_CLEAR, ActionAddition::NOT_ALLOWED);

	// if this setting is on, clearing of automation is restricted to automation view
	if (!FlashStorage::automationClear) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		currentSong->paramManager.deleteAllAutomation(action, modelStack);
	}

	// We go through deleting the ClipInstances one by one. This is actually quite inefficient, but complicated to
	// improve on because the deletion of the Clips themselves, where there are arrangement-only ones, causes the
	// calling of output->pickAnActiveClipIfPossible. So we have to ensure that extra ClipInstances don't exist at any
	// instant in time, or else it'll look at those to pick the new activeClip, which might not exist anymore.
	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		for (int32_t i = output->clipInstances.getNumElements() - 1; i >= 0; i--) {
			deleteClipInstance(output, i, output->clipInstances.getElement(i), action, false);
		}
	}

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

bool ArrangerView::opened() {

	mustRedrawTickSquares = true;

	focusRegained();

	bool renderingToStore = (currentUIMode == UI_MODE_ANIMATION_FADE);
	if (renderingToStore) {
		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight]);
		renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	}
	else {
		uiNeedsRendering(this);
	}

	return true;
}

void ArrangerView::setLedStates() {

	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, currentSong->arrangerAutoScrollModeActive);

#ifdef currentClipStatusButtonX
	view.switchOffCurrentClipPad();
#endif
}

void ArrangerView::focusRegained() {
	view.focusRegained();

	repopulateOutputsOnScreen(false);

	renderDisplay();
	if (currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) {
		view.setActiveModControllableTimelineCounter(currentSong);
	}

	indicator_leds::setLedState(IndicatorLED::BACK, false);

	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	setLedStates();

	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	currentSong->lastClipInstanceEnteredStartPos = 0;

	if (!doingAutoScrollNow) {
		reassessWhetherDoingAutoScroll(); // Can start, but can't stop
	}
}

void ArrangerView::repopulateOutputsOnScreen(bool doRender) {
	// First, clear out the Outputs onscreen
	memset(outputsOnScreen, 0, sizeof(outputsOnScreen));

	Output* output = currentSong->firstOutput;
	int32_t row = 0 - currentSong->arrangementYScroll;
	while (output) {
		if (row >= kDisplayHeight) {
			break;
		}
		if (row >= 0) {
			outputsOnScreen[row] = output;
		}
		row++;
		output = output->next;
	}

	mustRedrawTickSquares = true;

	if (doRender) {
		// use root UI in case this is called from performance view
		requestRendering(getRootUI());
	}
}

bool ArrangerView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (view.potentiallyRenderVUMeter(image)) {
		return true;
	}

	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (whichRows & (1 << i)) {
			image[i][kDisplayWidth] = getMutePadColor(i);
			image[i][kDisplayWidth + 1] = getAuditionPadColor(i);
		}
	}
	return true;
}

RGB ArrangerView::getMutePadColor(int32_t yDisplay) {
	// If no Instrument, black
	if (outputsOnScreen[yDisplay] == nullptr) {
		return colours::black;
	}

	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING && outputsOnScreen[yDisplay]->armedForRecording) {
		if (blinkOn) {
			if (outputsOnScreen[yDisplay]->wantsToBeginArrangementRecording()) {
				return RGB(255, 1, 0);
			}
			return RGB(60, 25, 15);
		}
		return colours::black;
	}

	// Soloing - blue
	if (outputsOnScreen[yDisplay]->soloingInArrangementMode) {
		return menu_item::soloColourMenu.getRGB();
	}

	// Or if not soloing...
	RGB colour = outputsOnScreen[yDisplay]->mutedInArrangementMode
	                 ? menu_item::mutedColourMenu.getRGB()   // Muted - yellow
	                 : menu_item::activeColourMenu.getRGB(); // Otherwise, green

	if (currentSong->getAnyOutputsSoloingInArrangement()) {
		colour = colour.dull();
	}
	return colour;
}

RGB ArrangerView::getAuditionPadColor(int32_t yDisplay) {
	RGB normal = (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION && yDisplay == yPressedEffective)
	                 ? colours::red
	                 : colours::black;

	Output* output = outputsOnScreen[yDisplay];

	if (!view.midiLearnFlashOn || output == nullptr || output->type == OutputType::AUDIO) {
		return normal;
	}

	auto* melodicInstrument = static_cast<Instrument*>(output);
	// If MIDI command already assigned...
	if (melodicInstrument->midiInput.containsSomething()) {
		return colours::midi_command;
	}

	// Or if not assigned but we're holding it down...
	if (view.thingPressedForMidiLearn == MidiLearn::INSTRUMENT_INPUT
	    && view.learnedThing == &melodicInstrument->midiInput) {
		return colours::red.dim();
	}

	return normal;
}

ModelStackWithNoteRow* ArrangerView::getNoteRowForAudition(ModelStack* modelStack, Kit* kit) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(kit->getActiveClip());

	ModelStackWithNoteRow* modelStackWithNoteRow;

	if (kit->getActiveClip()) {

		InstrumentClip* instrumentClip = (InstrumentClip*)kit->getActiveClip();
		modelStackWithNoteRow = instrumentClip->getNoteRowForDrumName(modelStackWithTimelineCounter, "SNAR");
		if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
			if (kit->selectedDrum) {
				modelStackWithNoteRow =
				    instrumentClip->getNoteRowForDrum(modelStackWithTimelineCounter, kit->selectedDrum);
			}
			if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
				modelStackWithNoteRow =
				    modelStackWithTimelineCounter->addNoteRow(0, instrumentClip->noteRows.getElement(0));
			}
		}
	}
	else {
		modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(0, nullptr);
	}

	return modelStackWithNoteRow;
}

Drum* ArrangerView::getDrumForAudition(Kit* kit) {
	Drum* drum = kit->getDrumFromName("SNAR");
	if (!drum) {
		drum = kit->selectedDrum;
		if (!drum) {
			drum = kit->firstDrum;
		}
	}

	return drum;
}

void ArrangerView::beginAudition(Output* output) {

	if (output->type == OutputType::AUDIO) {
		return;
	}

	Instrument* instrument = (Instrument*)output;

	if (!playbackHandler.playbackState && !Buttons::isShiftButtonPressed()) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		if (instrument->type == OutputType::KIT) {

			Kit* kit = (Kit*)instrument;
			ModelStackWithNoteRow* modelStackWithNoteRow = getNoteRowForAudition(modelStack, kit);

			Drum* drum;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				drum = noteRow->drum;
				if (drum && drum->type == DrumType::SOUND && !noteRow->paramManager.containsAnyMainParamCollections()) {
					FREEZE_WITH_ERROR("E324"); // Vinz got this! I may have since fixed.
				}
			}
			else {
				drum = getDrumForAudition(kit);
			}

			if (drum) {
				kit->beginAuditioningforDrum(modelStackWithNoteRow, drum, kit->defaultVelocity, zeroMPEValues);
			}
		}
		else {
			int32_t note = (currentSong->key.rootNote + 120) % 12;
			note += 60;
			((MelodicInstrument*)instrument)
			    ->beginAuditioningForNote(modelStack, note, instrument->defaultVelocity, zeroMPEValues);
		}
	}
}

void ArrangerView::endAudition(Output* output, bool evenIfPlaying) {

	if (output->type == OutputType::AUDIO) {
		return;
	}

	Instrument* instrument = (Instrument*)output;

	if (!playbackHandler.playbackState || evenIfPlaying) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		if (instrument->type == OutputType::KIT) {

			Kit* kit = (Kit*)instrument;
			ModelStackWithNoteRow* modelStackWithNoteRow = getNoteRowForAudition(modelStack, kit);

			Drum* drum;
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (noteRow) {
				drum = noteRow->drum;
			}
			else {
				drum = getDrumForAudition(kit);
			}

			if (drum) {
				kit->endAuditioningForDrum(modelStackWithNoteRow, drum);
			}
		}
		else {
			int32_t note = (currentSong->key.rootNote + 120) % 12;
			note += 60;
			((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, note);
		}
	}
}

// Loads from file, etc - doesn't truly "create"
Instrument* ArrangerView::createNewInstrument(OutputType newOutputType, bool* instrumentAlreadyInSong) {
	Error error = Browser::currentDir.set(getInstrumentFolder(newOutputType));
	if (error != Error::NONE) {
		display->displayError(error);
		return nullptr;
	}

	FileItem* fileItem = D_TRY_CATCH(loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(
	                                     currentSong, newOutputType, Availability::INSTRUMENT_UNUSED),
	                                 error, {
		                                 display->displayError(error);
		                                 return nullptr;
	                                 });

	Instrument* newInstrument = fileItem->instrument;
	bool isHibernating = newInstrument && !fileItem->instrumentAlreadyInSong;
	*instrumentAlreadyInSong = newInstrument && fileItem->instrumentAlreadyInSong;

	if (!newInstrument) {
		String newPresetName;
		fileItem->getDisplayNameWithoutExtension(&newPresetName);
		error = StorageManager::loadInstrumentFromFile(currentSong, nullptr, newOutputType, false, &newInstrument,
		                                               &fileItem->filePointer, &newPresetName, &Browser::currentDir);
	}

	Browser::emptyFileItems();

	if (error != Error::NONE) {
		display->displayError(error);
		return nullptr;
	}

	if (isHibernating) {
		currentSong->removeInstrumentFromHibernationList(newInstrument);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	newInstrument->setupWithoutActiveClip(modelStack);

	return newInstrument;
}

void ArrangerView::auditionPadAction(bool on, int32_t y, UI* ui) {

	int32_t note = (currentSong->key.rootNote + 120) % 12;
	note += 60;

	// Press on
	if (on) {

		if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
			endAudition(outputsOnScreen[yPressedEffective]);
			indicator_leds::setLedState(IndicatorLED::SYNTH, false);
			indicator_leds::setLedState(IndicatorLED::KIT, false);
			indicator_leds::setLedState(IndicatorLED::MIDI, false);
			indicator_leds::setLedState(IndicatorLED::CV, false);

			currentUIMode = UI_MODE_NONE;

			uiNeedsRendering(ui, 0x00000000, 0xFFFFFFFF);

			goto doNewPress;
		}

		if (currentUIMode == UI_MODE_NONE) {
doNewPress:

			Output* output = outputsOnScreen[y];

			yPressedEffective = y;
			yPressedActual = y;

			// If nothing on this row yet, we'll add a brand new Instrument
			if (!output) {

				int32_t minY = -currentSong->arrangementYScroll - 1;
				int32_t maxY = -currentSong->arrangementYScroll + currentSong->getNumOutputs();

				yPressedEffective = std::max((int32_t)yPressedEffective, minY);
				yPressedEffective = std::min((int32_t)yPressedEffective, maxY);

				bool instrumentAlreadyInSong; // Will always end up false

				output = createNewInstrument(OutputType::SYNTH, &instrumentAlreadyInSong);
				if (!output) {
					return;
				}

				if (!instrumentAlreadyInSong) {
					currentSong->addOutput(
					    output,
					    (yPressedEffective == -currentSong->arrangementYScroll - 1)); // This should always be triggered
				}

				outputsOnScreen[yPressedEffective] = output;
			}

			currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;

			view.displayOutputName(output);

			beginAudition(output);

			if (output->getActiveClip()) {
				view.setActiveModControllableTimelineCounter(output->getActiveClip());
			}
			else {
				view.setActiveModControllableWithoutTimelineCounter(output->toModControllable(),
				                                                    output->getParamManager(currentSong));
			}

			uiNeedsRendering(ui, 0, 1 << yPressedEffective);
		}
	}

	// Release press
	else {
		if (y == yPressedActual) {
			exitSubModeWithoutAction(ui);
		}
	}
}

void ArrangerView::auditionEnded() {
	setNoSubMode();
	setLedStates();

	if (getRootUI() == &automationView) {
		if (automationView.inAutomationEditor()) {
			automationView.displayAutomation(true, !display->have7SEG());
		}
		else {
			automationView.renderDisplay();
		}
	}
	else {
		renderDisplay();
	}

	view.setActiveModControllableTimelineCounter(currentSong);
}

ActionResult ArrangerView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// don't interact with sidebar if VU Meter is displayed
	// and you're in the volume/pan mod knob mode (0)
	if (x >= kDisplayWidth && view.displayVUMeter && (view.getModKnobMode() == 0)) {
		return ActionResult::DEALT_WITH;
	}

	// Audition pad
	if (x == kDisplayWidth + 1) {
		return handleAuditionPadAction(y, velocity, this);
	}

	// Status pad
	else if (x == kDisplayWidth) {
		return handleStatusPadAction(y, velocity, this);
	}

	// Edit pad
	else {
		return handleEditPadAction(x, y, velocity);
	}
}

ActionResult ArrangerView::handleEditPadAction(int32_t x, int32_t y, int32_t velocity) {
	Output* output = outputsOnScreen[y];

	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		if (velocity != 0) {
			// NAME shortcut
			if (x == 11 && y == 5) {
				Output* output = outputsOnScreen[yPressedEffective];
				if (output && output->type != OutputType::CV) {
					endAudition(output);
					currentUIMode = UI_MODE_NONE;
					renameOutputUI.output = output;
					openUI(&renameOutputUI);
					uiNeedsRendering(this, 0, 0xFFFFFFFF); // Stop audition pad being illuminated
				}
			}
		}
	}
	else {
		if (output) {
			editPadAction(x, y, velocity);
		}
	}
	return ActionResult::DEALT_WITH;
}

ActionResult ArrangerView::handleStatusPadAction(int32_t y, int32_t velocity, UI* ui) {
	Output* output = outputsOnScreen[y];

	if (!output) {
		return ActionResult::DEALT_WITH;
	}

	if (velocity != 0) {
		uint32_t rowsToRedraw = 1 << y;

		switch (currentUIMode) {
		case UI_MODE_VIEWING_RECORD_ARMING:
			output->armedForRecording = !output->armedForRecording;
			PadLEDs::reassessGreyout(true);
			return ActionResult::DEALT_WITH; // No need to draw anything

#ifdef soloButtonX
		case UI_MODE_SOLO_BUTTON_HELD:
#else
		case UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON:
#endif // Soloing
			if (!output->soloingInArrangementMode) {

				if (arrangement.hasPlaybackActive()) {

					// If other Instruments were already soloing, or if they weren't but this instrument was muted,
					// we'll need to tell it to start playing
					if (currentSong->getAnyOutputsSoloingInArrangement() || output->mutedInArrangementMode) {
						outputActivated(output);
					}
				}

				// If we're the first Instrument to be soloing, need to tell others they've been inadvertedly
				// deactivated
				if (!currentSong->getAnyOutputsSoloingInArrangement()) {
					for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
						if (thisOutput != output && !thisOutput->mutedInArrangementMode) {
							outputDeactivated(thisOutput);
						}
					}
				}

				// If no other soloing previously...
				if (!currentSong->anyOutputsSoloingInArrangement) {
					currentSong->anyOutputsSoloingInArrangement = true;
					rowsToRedraw = 0xFFFFFFFF; // Redraw other mute pads
				}

				output->soloingInArrangementMode = true;
			}

			// Unsoloing
			else {
doUnsolo:
				output->soloingInArrangementMode = false;
				currentSong->reassessWhetherAnyOutputsSoloingInArrangement();

				// If no more soloing, redraw other mute pads
				if (!currentSong->anyOutputsSoloingInArrangement) {
					rowsToRedraw = 0xFFFFFFFF;
				}

				// If any other Instruments still soloing, or if we're "muted", deactivate us
				if (currentSong->getAnyOutputsSoloingInArrangement() || output->mutedInArrangementMode) {
					outputDeactivated(output);
				}

				if (arrangement.hasPlaybackActive()) {

					// If no other Instruments still soloing, re-activate all the other ones
					if (!currentSong->getAnyOutputsSoloingInArrangement()) {
						for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
							if (thisOutput != output && !thisOutput->mutedInArrangementMode) {
								outputActivated(thisOutput);
							}
						}
					}
				}
			}
			break;

		case UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION:
			// If it's the mute pad for the same row we're auditioning, don't do anything. User might be subconsciously
			// repeating the "drag row" action for Kits in InstrumentClipView.
			if (y == yPressedEffective) {
				break;
			}
			goto regularMutePadPress; // Otherwise, do normal.

		case UI_MODE_NONE:
			// If the user was just quick and is actually holding the record button but the submode just hasn't changed
			// yet...
			if (velocity && Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
				output->armedForRecording = !output->armedForRecording;
				timerCallback();                 // Get into UI_MODE_VIEWING_RECORD_ARMING
				return ActionResult::DEALT_WITH; // No need to draw anything
			}
			// No break

		case UI_MODE_HOLDING_ARRANGEMENT_ROW:
regularMutePadPress:
			// If it's soloing, unsolo.
			if (output->soloingInArrangementMode) {
				goto doUnsolo;
			}

			// Unmuting
			if (output->mutedInArrangementMode) {
				output->mutedInArrangementMode = false;
				if (arrangement.hasPlaybackActive() && !currentSong->getAnyOutputsSoloingInArrangement()) {
					outputActivated(output);
				}
			}

			// Muting
			else {
				if (!currentSong->getAnyOutputsSoloingInArrangement()) {
					outputDeactivated(output);
				}
				output->mutedInArrangementMode = true;
			}
			break;
		default:
			break;
		}

		uiNeedsRendering(ui, 0, rowsToRedraw);
		mustRedrawTickSquares = true;
	}
	return ActionResult::DEALT_WITH;
}

ActionResult ArrangerView::handleAuditionPadAction(int32_t y, int32_t velocity, UI* ui) {
	Output* output = outputsOnScreen[y];

	switch (currentUIMode) {
	case UI_MODE_MIDI_LEARN:
		if (output) {
			if (output->type == OutputType::AUDIO) {
				if (velocity != 0) {
					view.endMIDILearn();
					context_menu::audioInputSelector.audioOutput = (AudioOutput*)output;
					context_menu::audioInputSelector.setupAndCheckAvailability();
					openUI(&context_menu::audioInputSelector);
				}
			}
			else {
				view.instrumentMidiLearnPadPressed(velocity, (MelodicInstrument*)output);
			}
		}
		break;

	default:
		auditionPadAction(velocity, y, ui);
		break;
	}
	return ActionResult::DEALT_WITH;
}

void ArrangerView::outputActivated(Output* output) {

	if (output->recordingInArrangement) {
		return;
	}

	int32_t actualPos = arrangement.getLivePos();

	int32_t i = output->clipInstances.search(actualPos + 1, LESS);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (clipInstance && clipInstance->pos + clipInstance->length > actualPos) {
		arrangement.resumeClipInstancePlayback(clipInstance);
	}

	playbackHandler.expectEvent(); // In case it doesn't get called by the above call instead
}

void ArrangerView::outputDeactivated(Output* output) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	output->stopAnyAuditioning(modelStack);

	if (arrangement.hasPlaybackActive()) {
		if (output->getActiveClip() && !output->recordingInArrangement) {
			output->getActiveClip()->expectNoFurtherTicks(currentSong);
			output->getActiveClip()->activeIfNoSolo = false;
		}
	}
}

// For now, we're always supplying clearingWholeArrangement as false, even when we are doing that
void ArrangerView::deleteClipInstance(Output* output, int32_t clipInstanceIndex, ClipInstance* clipInstance,
                                      Action* action, bool clearingWholeArrangement) {

	if (action) {
		action->recordClipInstanceExistenceChange(output, clipInstance, ExistenceChangeType::DELETE);
	}
	Clip* clip = clipInstance->clip;

	// Delete the ClipInstance
	if (!clearingWholeArrangement) {
		output->clipInstances.deleteAtIndex(clipInstanceIndex);
	}

	currentSong->deletingClipInstanceForClip(output, clip, action, !clearingWholeArrangement);
}

void ArrangerView::interactWithClipInstance(Output* output, int32_t yDisplay, ClipInstance* clipInstance) {
	Clip* clip = clipInstance->clip;

	if (clip) {
		// it looks like this variable isn't used anywhere
		originallyPressedClipActualLength = clipInstance->clip->loopLength;

		// we've changed the clip selected, so set clip to current
		currentSong->setCurrentClip(clip);
	}
	else {
		originallyPressedClipActualLength = clipInstance->length;
	}

	rememberInteractionWithClipInstance(yDisplay, clipInstance);

	view.displayOutputName(output, true, clip);

	// this needs to be called after the current clip is set and after
	// the interaction with clip instance is remembered in order to ensure that
	// if midi follow feedback is enabled, it sends feedback for the right clip instance
	view.setActiveModControllableTimelineCounter(clip);

	// Set current clip for visualizer when holding clip in arranger view
	// This allows visualizer to show clip-specific waveform when clip is held
	deluge::hid::display::Visualizer::trySetClipForVisualizer(clip);
}

void ArrangerView::rememberInteractionWithClipInstance(int32_t yDisplay, ClipInstance* clipInstance) {
	lastInteractedOutputIndex = yDisplay + currentSong->arrangementYScroll;
	lastInteractedPos = clipInstance->pos;
	lastInteractedSection = clipInstance->clip ? clipInstance->clip->section : 255;
	lastInteractedClipInstance = clipInstance;
}

void ArrangerView::editPadAction(int32_t x, int32_t y, bool on) {
	Output* output = outputsOnScreen[y];
	uint32_t xScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];

	// Shift button pressed - clone ClipInstance to white / unique
	if (Buttons::isShiftButtonPressed() && (currentUIMode == UI_MODE_NONE)) {
		if (on) {
			cloneClipInstanceToWhite(output, x, y, xScroll);
		}
	}

	else {
		// Press
		if (on) {

			int32_t squareStart = getPosFromSquare(x, xScroll);
			int32_t squareEnd = getPosFromSquare(x + 1, xScroll);

			if (squareStart >= squareEnd) {
				FREEZE_WITH_ERROR("E210");
			}

			// No previous press
			if (currentUIMode == UI_MODE_NONE) {
				createNewClipInstance(output, x, y, squareStart, squareEnd, xScroll);
				lastInteractedArrangementPos = squareStart;
			}

			// Already pressing - length edit
			else if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW) {
				// Only when pressing on the same row, and to the right of the currently held pad
				if (y == yPressedEffective && x > xPressed) {
					adjustClipInstanceLength(output, xPressed, y, squareStart, squareEnd);
				}
			}
		}

		// Release press
		else {

			if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW)) {

				// If also stuttering, stop that
				if (isUIModeActive(UI_MODE_STUTTERING)) {
					((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
					    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
				}

				if (x == xPressed && y == yPressedEffective) {
					// if a section clip instance was changed to an arrangement only clip instance,
					// the clip itself is created on pad release
					if (lastInteractedSection == 255 && !lastInteractedClipInstance->clip) {
						createNewClipForClipInstance(output, lastInteractedClipInstance);
					}

					// If no action to perform...
					if (!actionOnDepress || (int32_t)(AudioEngine::audioSampleTimer - pressTime) >= kShortPressTime) {
						return exitSubModeWithoutAction();
					}

					// Or if yes we do want to do some action...
					else {
						ClipInstance* clipInstance =
						    output->clipInstances.getElement(pressedClipInstanceIndex); // Can't fail, I think?

						// If pressed head, delete
						if (pressedHead) {
							deleteClipInstance(output, clipInstance);
						}

						// Otherwise, go into Clip
						else {

							// In this case, we leave the activeModControllableClip the same

							// If Clip wasn't created yet, create it first. This does both AudioClips and
							// InstrumentClips
							if (!clipInstance->clip) {
								createNewClipForClipInstance(output, clipInstance);
							}

							if (clipInstance->clip) {
								transitionToClipView(clipInstance);
							}
						}
					}
				}
			}
		}
	}
}

/// called from ArrangerView::editPadAction
/// converts an existing clip instance to white when holding shift and pressing a clip instance pad
void ArrangerView::cloneClipInstanceToWhite(Output* output, int32_t x, int32_t y, int32_t xScroll) {
	int32_t squareStart = getPosFromSquare(x, xScroll);
	int32_t squareEnd = getPosFromSquare(x + 1, xScroll);

	int32_t i = output->clipInstances.search(squareEnd, LESS);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (clipInstance && clipInstance->pos + clipInstance->length >= squareStart) {
		Clip* oldClip = clipInstance->clip;

		if (oldClip && !oldClip->isArrangementOnlyClip() && !oldClip->getCurrentlyRecordingLinearly()) {
			actionLogger.deleteAllLogs();

			Error error = arrangement.doUniqueCloneOnClipInstance(clipInstance, clipInstance->length, true);
			if (error != Error::NONE) {
				display->displayError(error);
			}
			else {
				recordEditPadPress(output, clipInstance, x, y, xScroll);
				uiNeedsRendering(this, 1 << y, 0);
			}
		}
	}
}

/// called from ArrangerView::editPadAction
/// tries to create a new clip instance when pressing an empty pad on the grid
void ArrangerView::createNewClipInstance(Output* output, int32_t x, int32_t y, int32_t squareStart, int32_t squareEnd,
                                         int32_t xScroll) {
	output->clipInstances.testSequentiality("E117");

	// Look for a clip instance to the left of, or including press position
	int32_t i = output->clipInstances.search(squareEnd, LESS);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);

	// If we did find a clip instance
	if (clipInstance) {
		Clip* clip = clipInstance->clip;

		if (clip) {
			// If it's being recorded to, some special instructions
			if (playbackHandler.playbackState && output->recordingInArrangement
			    && clip->getCurrentlyRecordingLinearly()) {
				// Can't press here to the left of the play/record cursor!
				if (squareStart < arrangement.getLivePos()) {
					return;
				}
				// To the right is allowed
				else {
					clipInstance = createClipInstance(output, y, squareStart);
				}
			}
		}

		// Or, normal case where not recording to Clip. If it actually finishes to our left, we can still go
		// ahead and make a new Instance here
		int32_t instanceEnd = clipInstance->pos + clipInstance->length;
		if (instanceEnd <= squareStart) {
			clipInstance = createClipInstance(output, y, squareStart);
		}
		else {
			// If still here, the ClipInstance overlaps this square, so select it
			pressedClipInstanceIndex = i;

			pressedHead = (clipInstance->pos >= squareStart);

			actionOnDepress = true;
		}
	}

	// Or, if no ClipInstance anywhere to the left, make a new one
	else {
		clipInstance = createClipInstance(output, y, squareStart);
	}

	recordEditPadPress(output, clipInstance, x, y, xScroll);
}

/// called from ArrangerView::createNewClipInstance
/// tries to create a new clip instance and assign a clip to that instance
ClipInstance* ArrangerView::createClipInstance(Output* output, int32_t y, int32_t squareStart) {
	ClipInstance* clipInstance = nullptr;
	// Decide what Clip / section to make this new ClipInstance
	Clip* newClip;

	Output* lastOutputInteractedWith = currentSong->getOutputFromIndex(lastInteractedOutputIndex);
	int32_t lastClipInstanceI = lastOutputInteractedWith->clipInstances.search(lastInteractedPos, GREATER_OR_EQUAL);
	ClipInstance* lastClipInstance = lastOutputInteractedWith->clipInstances.getElement(lastClipInstanceI);

	// Test thing
	{
		int32_t j = output->clipInstances.search(squareStart, GREATER_OR_EQUAL);
		ClipInstance* nextClipInstance = output->clipInstances.getElement(j);
		if (nextClipInstance && nextClipInstance->pos == squareStart) {
			FREEZE_WITH_ERROR("E233"); // Yes, this happened to someone. Including me!!
		}
	}

	newClip = getClipForNewClipInstance(output, lastOutputInteractedWith, lastClipInstance);

	// Make the actual new ClipInstance. Do it now, after potentially looking at existing ones above, so
	// that we don't look at this new one above
	pressedClipInstanceIndex = output->clipInstances.insertAtKey(squareStart);

	// Test thing
	{
		ClipInstance* nextInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
		if (nextInstance) {
			if (nextInstance->pos == squareStart) {
				FREEZE_WITH_ERROR("E232");
			}
		}
	}

	clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);
	if (!clipInstance) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	clipInstance->clip = newClip;

	if (clipInstance->clip) {
		clipInstance->length = clipInstance->clip->loopLength;
	}
	else {
		clipInstance->length = kDefaultClipLength << currentSong->insideWorldTickMagnitude;
	}

	if (clipInstance->length < 1) {
		FREEZE_WITH_ERROR("E049");
	}

	ClipInstance* nextInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
	if (nextInstance) {

		if (nextInstance->pos == squareStart) {
			FREEZE_WITH_ERROR("E209");
		}

		int32_t maxLength = nextInstance->pos - squareStart;
		if (clipInstance->length > maxLength) {
			clipInstance->length = maxLength;
			if (clipInstance->length < 1) {
				FREEZE_WITH_ERROR("E048");
			}
		}
	}

	if (clipInstance->length > kMaxSequenceLength - clipInstance->pos) {
		clipInstance->length = kMaxSequenceLength - clipInstance->pos;
		if (clipInstance->length < 1) {
			FREEZE_WITH_ERROR("E045");
		}
	}

	Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::NOT_ALLOWED);
	if (action) {
		action->recordClipInstanceExistenceChange(output, clipInstance, ExistenceChangeType::CREATE);
	}

	arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, nullptr, clipInstance);

	uiNeedsRendering(this, 1 << y, 0);

	actionOnDepress = false;
	pressedHead = true;

	return clipInstance;
}

/// called from ArrangerView::createClipInstance
/// get clip to assign to new clip instance being created
/// will not create a new clip, only retrieves existing clips
Clip* ArrangerView::getClipForNewClipInstance(Output* output, Output* lastOutputInteractedWith,
                                              ClipInstance* lastClipInstance) {
	Clip* newClip = nullptr;
	if (lastClipInstance && lastClipInstance->pos == lastInteractedPos) {

		// If same Output...
		if (lastOutputInteractedWith == output) {
			if (lastClipInstance->clip && !lastClipInstance->clip->isArrangementOnlyClip()) {
				newClip = lastClipInstance->clip;
			}
		}

		// Or if different Output...
		else {
			// If yes Clip, look for another one with that section
			if (lastClipInstance->clip) {
				lastInteractedSection = lastClipInstance->clip->section;
				newClip = getClipFromSection(output);
			}
		}
	}
	else {
		newClip = getClipFromSection(output);
	}

	return newClip;
}

/// called from ArrangerView::getClipForNewClipInstance
/// tries to get a clip from the last section the user interacted with
Clip* ArrangerView::getClipFromSection(Output* output) {
	Clip* newClip = nullptr;
	if (lastInteractedSection != 255) {
		newClip = currentSong->getSessionClipWithOutput(output, lastInteractedSection);

		// If that section had none, just get any old one (still might return NULL - that's fine)
		if (!newClip) {
			newClip = currentSong->getSessionClipWithOutput(output);
		}
	}
	return newClip;
}

/// called from ArrangerView::editPadAction
/// adjust the length of an existing clip instance
void ArrangerView::adjustClipInstanceLength(Output* output, int32_t x, int32_t y, int32_t squareStart,
                                            int32_t squareEnd) {
	actionOnDepress = false;

	if (!pressedClipInstanceIsInValidPosition) {
		return;
	}

	int32_t oldSquareStart = getPosFromSquare(x);
	int32_t oldSquareEnd = getPosFromSquare(x + 1);

	// Search for previously pressed ClipInstance
	ClipInstance* clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);

	int32_t lengthTilNewSquareStart = squareStart - clipInstance->pos;

	desiredLength = clipInstance->length; // I don't think this should still be here...

	// Shorten
	if (clipInstance->length > lengthTilNewSquareStart) {
		Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::ALLOWED);
		if (clipInstance->clip) {
			arrangement.rowEdited(output, clipInstance->pos + lengthTilNewSquareStart,
			                      clipInstance->pos + clipInstance->length, clipInstance->clip, nullptr);
		}
		clipInstance->change(action, output, clipInstance->pos, lengthTilNewSquareStart, clipInstance->clip);
	}

	// Lengthen
	else {

		int32_t oldLength = clipInstance->length;

		int32_t newLength = squareEnd - clipInstance->pos;

		// Make sure it doesn't collide with next ClipInstance
		ClipInstance* nextClipInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
		if (nextClipInstance) {
			int32_t maxLength = nextClipInstance->pos - clipInstance->pos;
			if (newLength > maxLength) {
				newLength = maxLength;
			}
		}

		if (newLength > kMaxSequenceLength - clipInstance->pos) {
			newLength = kMaxSequenceLength - clipInstance->pos;
		}

		// If we are in fact able to lengthen it...
		if (newLength > oldLength) {

			Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::ALLOWED);

			clipInstance->change(action, output, clipInstance->pos, newLength, clipInstance->clip);
			arrangement.rowEdited(output, clipInstance->pos + oldLength, clipInstance->pos + clipInstance->length,
			                      nullptr, clipInstance);
		}
	}

	desiredLength = clipInstance->length;

	uiNeedsRendering(this, 1 << y, 0);
}

/// called from ArrangerView::editPadAction
/// deletes a clip instance when short pressing the head of a clip instance
void ArrangerView::deleteClipInstance(Output* output, ClipInstance* clipInstance) {
	// set lastInteractedClipInstance to null so you don't send midi follow feedback for a
	// deleted clip
	lastInteractedClipInstance = nullptr;
	view.setActiveModControllableTimelineCounter(currentSong);

	arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, clipInstance->clip,
	                      nullptr);

	Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::NOT_ALLOWED);
	deleteClipInstance(output, pressedClipInstanceIndex, clipInstance, action);

	deluge::hid::display::OLED::clearMainImage();
	return exitSubModeWithoutAction();
}

/// called from ArrangerView::editPadAction
/// creates a new clip when trying to enter a clip instance that does not have a clip assigned to it
void ArrangerView::createNewClipForClipInstance(Output* output, ClipInstance* clipInstance) {
	if (!currentSong->arrangementOnlyClips.ensureEnoughSpaceAllocated(1)) {
		return exitSubModeWithoutAction();
	}

	int32_t size = (output->type == OutputType::AUDIO) ? sizeof(AudioClip) : sizeof(InstrumentClip);

	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(size);
	if (!memory) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return exitSubModeWithoutAction();
	}

	Clip* newClip;

	if (output->type == OutputType::AUDIO)
		newClip = new (memory) AudioClip();
	else
		newClip = new (memory) InstrumentClip(currentSong);

	newClip->loopLength = clipInstance->length;
	newClip->section = 255;
	newClip->activeIfNoSolo = false; // Always need to set arrangement-only Clips like this on create

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, newClip);

	Error error;

	if (output->type == OutputType::AUDIO) {
		error = ((AudioClip*)newClip)->setOutput(modelStack, output);
	}
	else {
		error = ((InstrumentClip*)newClip)->setInstrument((Instrument*)output, currentSong, nullptr);
	}

	if (error != Error::NONE) {
		display->displayError(error);
		newClip->~Clip();
		delugeDealloc(memory);
		return exitSubModeWithoutAction();
	}

	if (output->type != OutputType::AUDIO) {
		((Instrument*)output)->setupPatching(modelStack);
		((InstrumentClip*)newClip)->setupAsNewKitClipIfNecessary(modelStack);
	}

	// Possibly want to set this as the activeClip, if Instrument didn't have one yet.
	// Crucial that we do this not long after calling setInstrument, in case this is the
	// first Clip with the Instrument and we just grabbed the backedUpParamManager for it,
	// which it might go and look for again if the audio routine was called in the interim
	if (!output->getActiveClip()) {
		output->setActiveClip(modelStack);
	}

	currentSong->arrangementOnlyClips.insertClipAtIndex(newClip, 0);

	Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::NOT_ALLOWED);
	if (action) {
		action->recordClipExistenceChange(currentSong, &currentSong->arrangementOnlyClips, newClip,
		                                  ExistenceChangeType::CREATE);
	}

	clipInstance->change(action, output, clipInstance->pos, clipInstance->length, newClip);

	arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, nullptr, clipInstance);
}

/// called from ArrangerView::editPadAction
/// records the action of selecting and holding a clip instance
void ArrangerView::recordEditPadPress(Output* output, ClipInstance* clipInstance, int32_t x, int32_t y,
                                      int32_t xScroll) {
	xPressed = x;
	yPressedEffective = y;
	yPressedActual = y;
	currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW;
	pressTime = AudioEngine::audioSampleTimer;
	desiredLength = clipInstance->length;
	pressedClipInstanceXScrollWhenLastInValidPosition = xScroll;
	pressedClipInstanceIsInValidPosition = true;
	pressedClipInstanceOutput = output;

	interactWithClipInstance(output, y, clipInstance);
}

// Only call if this is the currentUI. May be called during audio / playback routine
// exception: this will be called when using the audition pad in arranger automation view
// in this exceptional case, the UI for automation view is passed so that the audition pad can be redrawn
void ArrangerView::exitSubModeWithoutAction(UI* ui) {
	if (ui == nullptr) {
		ui = this;
	}

	// First, stop any stuttering. This may then put us back in one of the subModes dealt with below
	if (isUIModeActive(UI_MODE_STUTTERING)) {
		((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
		    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
	}

	// --------------

	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		// Clear clip visualizer when clip audition ends (return to global visualizer)
		deluge::hid::display::Visualizer::clearClipForVisualizer();

		Output* output = outputsOnScreen[yPressedEffective];
		if (output) {
			endAudition(output);
			auditionEnded();
			uiNeedsRendering(ui, 0, 1 << yPressedEffective);
		}
	}

	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW)) {
		// Clear clip visualizer when clip press ends (return to global visualizer)
		deluge::hid::display::Visualizer::clearClipForVisualizer();

		// needs to be set before setActiveModControllableTimelineCounter so that midi follow mode can get
		// the right model stack with param (otherwise midi follow mode will think you're still in a clip)
		setNoSubMode();
		view.setActiveModControllableTimelineCounter(currentSong);
		uint32_t whichRowsNeedReRendering;
		if (outputsOnScreen[yPressedEffective] == pressedClipInstanceOutput) {
			whichRowsNeedReRendering = (1 << yPressedEffective);
		}
		else {
			whichRowsNeedReRendering = 0xFFFFFFFF;
		}
		renderDisplay();
		uiNeedsRendering(ui, whichRowsNeedReRendering, 0);
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		actionLogger.closeAction(ActionType::CLIP_INSTANCE_EDIT);
	}

	if (isUIModeActive(UI_MODE_MIDI_LEARN)) {
		view.endMIDILearn();
	}
}

/// redraw OLED and 7SEG displays
void ArrangerView::renderDisplay() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		sessionView.redrawNumericDisplay();
	}
}

/// enter clip view
void ArrangerView::transitionToClipView(ClipInstance* clipInstance) {

	Clip* clip = clipInstance->clip;
	// it should already be this clip, but if it ever isn't it would be a disaster
	currentSong->setCurrentClip(clip);

	currentSong->lastClipInstanceEnteredStartPos = clipInstance->pos;

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];
	while ((xZoom >> 1) * kDisplayWidth >= clip->loopLength) {
		xZoom >>= 1;
	}
	currentSong->xZoom[NAVIGATION_CLIP] = xZoom;

	// If can see whole Clip at zoom level, set scroll to 0
	if (xZoom * kDisplayWidth >= clip->loopLength) {
		currentSong->xScroll[NAVIGATION_CLIP] = 0;
	}

	// Otherwise...
	else {
		int32_t newScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT] - clipInstance->pos;
		if (newScroll < 0) {
			newScroll = 0;
		}
		else {
			newScroll = (uint32_t)newScroll % (uint32_t)clip->loopLength;
			newScroll = (uint32_t)newScroll / (xZoom * kDisplayWidth) * (xZoom * kDisplayWidth);
		}

		currentSong->xScroll[NAVIGATION_CLIP] = newScroll;
	}

	currentUIMode = UI_MODE_EXPLODE_ANIMATION;

	bool onKeyboardScreen = ((clip->type == ClipType::INSTRUMENT) && ((InstrumentClip*)clip)->onKeyboardScreen);

	// when transitioning back to clip, if keyboard view is enabled, it takes precedent
	// over automation and instrument clip views.
	if (clip->onAutomationClipView && !onKeyboardScreen) {
		PadLEDs::explodeAnimationYOriginBig = yPressedEffective << 16;

		if (clip->type == ClipType::INSTRUMENT) {
			instrumentClipView.recalculateColours();
		}

		automationView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1], false);
	}
	else if (clip->type == ClipType::AUDIO) {
		// If no sample, just skip directly there
		if (!((AudioClip*)clip)->sampleHolder.audioFile) {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audioClipView);

			return;
		}
		else {
			waveformRenderer.collapseAnimationToWhichRow = yPressedEffective;

			int64_t xScrollSamples;
			int64_t xZoomSamples;

			((AudioClip*)clip)
			    ->getScrollAndZoomInSamples(currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP],
			                                &xScrollSamples, &xZoomSamples);

			waveformRenderer.findPeaksPerCol((Sample*)((AudioClip*)clip)->sampleHolder.audioFile, xScrollSamples,
			                                 xZoomSamples, &((AudioClip*)clip)->renderData);

			PadLEDs::setupAudioClipCollapseOrExplodeAnimation((AudioClip*)clip);
		}
	}
	else {
		PadLEDs::explodeAnimationYOriginBig = yPressedEffective << 16;

		// If going to KeyboardView...
		if (onKeyboardScreen) {
			keyboardScreen.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);
			memset(PadLEDs::occupancyMaskStore[0], 0, kDisplayWidth + kSideBarWidth);
			memset(PadLEDs::occupancyMaskStore[kDisplayHeight + 1], 0, kDisplayWidth + kSideBarWidth);
		}

		// Or if just regular old InstrumentClipView
		else {
			instrumentClipView.recalculateColours();
			instrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1],
			                                  false);
			instrumentClipView.fillOffScreenImageStores();
		}
	}

	int32_t start = instrumentClipView.getPosFromSquare(0);
	int32_t end = instrumentClipView.getPosFromSquare(kDisplayWidth);

	int64_t xStartBig = getSquareFromPos(clipInstance->pos + start) << 16;

	int64_t clipLengthBig = ((uint64_t)clip->loopLength << 16) / currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	if (clipLengthBig) {
		while (true) {
			int64_t nextPotentialStart = xStartBig + clipLengthBig;
			if ((nextPotentialStart >> 16) > xPressed) {
				break;
			}
			xStartBig = nextPotentialStart;
		}
	}

	PadLEDs::explodeAnimationXStartBig = xStartBig;
	PadLEDs::explodeAnimationXWidthBig = ((uint32_t)(end - start) / currentSong->xZoom[NAVIGATION_ARRANGEMENT]) << 16;

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
	PadLEDs::explodeAnimationDirection = 1;
	if ((clip->type == ClipType::AUDIO) && !clip->onAutomationClipView) {
		PadLEDs::renderAudioClipExplodeAnimation(0);
	}
	else {
		PadLEDs::renderExplodeAnimation(0);
	}
	PadLEDs::sendOutSidebarColours(); // They'll have been cleared by the first explode render

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_CLIP_VIEW);
}

// Returns false if error
bool ArrangerView::transitionToArrangementEditor() {

	Sample* sample;

	if (getCurrentClip()->type == ClipType::AUDIO && getCurrentUI() != &automationView) {

		// If no sample, just skip directly there
		if (!getCurrentAudioClip()->sampleHolder.audioFile) {
			changeRootUI(&arrangerView);
			return true;
		}
	}

	Output* output = getCurrentOutput();
	int32_t i = output->clipInstances.search(currentSong->lastClipInstanceEnteredStartPos, GREATER_OR_EQUAL);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (!clipInstance || clipInstance->clip != getCurrentClip()) {
		return false;
	}

	int32_t start = instrumentClipView.getPosFromSquare(0);
	int32_t end = instrumentClipView.getPosFromSquare(kDisplayWidth);

	currentUIMode = UI_MODE_IMPLODE_ANIMATION;

	memcpy(PadLEDs::imageStore[1], PadLEDs::image, (kDisplayWidth + kSideBarWidth) * kDisplayHeight * sizeof(RGB));
	memcpy(PadLEDs::occupancyMaskStore[1], PadLEDs::occupancyMask, (kDisplayWidth + kSideBarWidth) * kDisplayHeight);
	if (getCurrentUI() == &instrumentClipView) {
		instrumentClipView.fillOffScreenImageStores();
	}

	int32_t outputIndex = currentSong->getOutputIndex(output);
	int32_t yDisplay = outputIndex - currentSong->arrangementYScroll;
	if (yDisplay < 0) {
		currentSong->arrangementYScroll += yDisplay;
		yDisplay = 0;
	}
	else if (yDisplay >= kDisplayHeight) {
		currentSong->arrangementYScroll += (yDisplay - kDisplayHeight + 1);
		yDisplay = kDisplayHeight - 1;
	}

	if (getCurrentClip()->type == ClipType::AUDIO && getCurrentUI() != &automationView) {
		waveformRenderer.collapseAnimationToWhichRow = yDisplay;

		PadLEDs::setupAudioClipCollapseOrExplodeAnimation(getCurrentAudioClip());
	}
	else {
		PadLEDs::explodeAnimationYOriginBig = yDisplay << 16;
	}

	int64_t clipLengthBig = ((uint64_t)getCurrentClip()->loopLength << 16) / currentSong->xZoom[NAVIGATION_ARRANGEMENT];
	int64_t xStartBig = getSquareFromPos(clipInstance->pos + start) << 16;

	int64_t potentialMidClip = xStartBig + (clipLengthBig >> 1);

	int32_t numExtraRepeats = (uint32_t)(clipInstance->length - 1) / (uint32_t)getCurrentClip()->loopLength;

	int64_t midClipDistanceFromMidDisplay;

	for (int32_t i = 0; i < numExtraRepeats; i++) {
		if (i == 0) {
			midClipDistanceFromMidDisplay = potentialMidClip - ((kDisplayWidth >> 1) << 16);
			if (midClipDistanceFromMidDisplay < 0) {
				midClipDistanceFromMidDisplay = -midClipDistanceFromMidDisplay;
			}
		}

		int64_t nextPotentialStart = xStartBig + clipLengthBig;
		potentialMidClip = nextPotentialStart + (clipLengthBig >> 1);

		int64_t newMidClipDistanceFromMidDisplay = potentialMidClip - ((kDisplayWidth >> 1) << 16);
		if (newMidClipDistanceFromMidDisplay < 0) {
			newMidClipDistanceFromMidDisplay = -newMidClipDistanceFromMidDisplay;
		}

		if (newMidClipDistanceFromMidDisplay >= midClipDistanceFromMidDisplay) {
			break;
		}
		xStartBig = nextPotentialStart;
		midClipDistanceFromMidDisplay = newMidClipDistanceFromMidDisplay;
	}

	PadLEDs::explodeAnimationXStartBig = xStartBig;
	PadLEDs::explodeAnimationXWidthBig = ((end - start) / currentSong->xZoom[NAVIGATION_ARRANGEMENT]) << 16;

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
	PadLEDs::explodeAnimationDirection = -1;

	// clear sidebar for instrumentClipView, automationClipView, and keyboardScreen
	if (getCurrentUI() != &audioClipView) {
		PadLEDs::clearSideBar();
	}

	PadLEDs::explodeAnimationTargetUI = this;
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 35);

	doingAutoScrollNow = false; // May get changed back at new scroll pos soon

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_TRANSITION_TO_ARRANGER_VIEW);

	return true;
}

// Wait.... kinda extreme that this seems to be able to happen during card routine even... is that dangerous?
// Seems to return whether it managed to put it in a new, valid position.
bool ArrangerView::putDraggedClipInstanceInNewPosition(Output* newOutputToDragInto) {

	uint32_t xScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	int32_t xMovement = xScroll - pressedClipInstanceXScrollWhenLastInValidPosition;

	ClipInstance* clipInstance = pressedClipInstanceOutput->clipInstances.getElement(pressedClipInstanceIndex);
	Clip* clip = clipInstance->clip;

	// If Output still the same
	if (newOutputToDragInto == pressedClipInstanceOutput) {

		// If back to original (well, last valid) scroll pos too, nothing to do
		if (!xMovement) {
			pressedClipInstanceIsInValidPosition = true;
			return true;
		}
	}

	// Or if Output not the same
	else {
		if (clip) {
			if (newOutputToDragInto->type != OutputType::AUDIO
			    || pressedClipInstanceOutput->type != OutputType::AUDIO) {
itsInvalid:
				pressedClipInstanceIsInValidPosition = false;
				blinkOn = false;
				uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kFastFlashTime);
				return false;
			}

			else if (currentSong->doesOutputHaveActiveClipInSession(newOutputToDragInto)) {
				goto itsInvalid;
			}
		}
	}

	int32_t newStartPos = clipInstance->pos + xMovement;

	// If moved left beyond 0
	if (newStartPos < 0) {
		goto itsInvalid;
	}

	// If moved right beyond numerical limit
	if (newStartPos > kMaxSequenceLength - clipInstance->length) {
		goto itsInvalid;
	}

	// See what's before
	int32_t iPrev = newOutputToDragInto->clipInstances.search(newStartPos, LESS);
	ClipInstance* prevClipInstance = newOutputToDragInto->clipInstances.getElement(iPrev);
	if (prevClipInstance != clipInstance) {
		if (prevClipInstance) {
			if (newOutputToDragInto->recordingInArrangement) {
				if (newStartPos <= arrangement.getLivePos()) {
					goto itsInvalid;
				}
			}
			else {
				if (prevClipInstance->pos + prevClipInstance->length > newStartPos) {
					goto itsInvalid;
				}
			}
		}
	}

	// See what's after
	int32_t iNext = iPrev + 1;
	ClipInstance* nextClipInstance = newOutputToDragInto->clipInstances.getElement(iNext);
	if (nextClipInstance != clipInstance) {
		if (nextClipInstance && nextClipInstance->pos < newStartPos + clipInstance->length) {
			goto itsInvalid;
		}
	}

	pressedClipInstanceIsInValidPosition = true;

	int32_t length = clipInstance->length;

	if (clip) {
		arrangement.rowEdited(pressedClipInstanceOutput, clipInstance->pos, clipInstance->pos + length, clip, nullptr);
	}

	Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::ALLOWED);

	// If order of elements hasn't changed and Output hasn't either...
	if (newOutputToDragInto == pressedClipInstanceOutput
	    && (prevClipInstance == clipInstance || nextClipInstance == clipInstance)) {
		clipInstance->change(action, newOutputToDragInto, newStartPos, clipInstance->length, clipInstance->clip);
	}

	// Or if it has...
	else {
		if (action) {
			action->recordClipInstanceExistenceChange(pressedClipInstanceOutput, clipInstance,
			                                          ExistenceChangeType::DELETE);
		}
		pressedClipInstanceOutput->clipInstances.deleteAtIndex(pressedClipInstanceIndex);

		pressedClipInstanceIndex = newOutputToDragInto->clipInstances.insertAtKey(newStartPos);
		// TODO: error check
		clipInstance = newOutputToDragInto->clipInstances.getElement(pressedClipInstanceIndex);
		clipInstance->clip = clip;
		clipInstance->length = length;
		if (action) {
			action->recordClipInstanceExistenceChange(newOutputToDragInto, clipInstance, ExistenceChangeType::CREATE);
		}

		// And if changing output...
		if (newOutputToDragInto != pressedClipInstanceOutput && clip) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

			((AudioClip*)clip)->changeOutput(modelStack->addTimelineCounter(clip), newOutputToDragInto);

			pressedClipInstanceOutput->pickAnActiveClipIfPossible(modelStack, true);

			view.setActiveModControllableTimelineCounter(clip);
		}

		pressedClipInstanceOutput = newOutputToDragInto;
	}

	if (clip) {
		arrangement.rowEdited(newOutputToDragInto, clipInstance->pos, clipInstance->pos + length, nullptr,
		                      clipInstance);
	}

	pressedClipInstanceXScrollWhenLastInValidPosition = xScroll;
	rememberInteractionWithClipInstance(yPressedEffective, clipInstance);

	uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	return true;
}

// Returns which rows couldn't be rendered
// occupancyMask can be NULL
uint32_t ArrangerView::doActualRender(int32_t xScroll, uint32_t xZoom, uint32_t whichRows, RGB* image,
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
                                      int32_t imageWidth) {
	uint32_t whichRowsCouldntBeRendered = 0;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (whichRows & (1 << yDisplay)) {
			uint8_t* occupancyMaskThisRow = nullptr;
			if (occupancyMask) {
				occupancyMaskThisRow = occupancyMask[yDisplay];
			}

			bool success = renderRow(modelStack, yDisplay, xScroll, xZoom, image, occupancyMaskThisRow, renderWidth);
			if (!success) {
				whichRowsCouldntBeRendered |= (1 << yDisplay);
			}
		}

		image += imageWidth;
	}

	return whichRowsCouldntBeRendered;
}

bool ArrangerView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	PadLEDs::renderingLock = true;

	uint32_t whichRowsCouldntBeRendered =
	    doActualRender(currentSong->xScroll[NAVIGATION_ARRANGEMENT], currentSong->xZoom[NAVIGATION_ARRANGEMENT],
	                   whichRows, &image[0][0], occupancyMask, kDisplayWidth, kDisplayWidth + kSideBarWidth);

	// Add negative region indicator when dragging clip instances left to the start
	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW) && currentSong->xScroll[NAVIGATION_ARRANGEMENT] < 0) {

		// Light up all columns that represent negative time positions with standard dim grey
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			for (int32_t xDisplay = 0; xDisplay < getSquareFromPos(0); xDisplay++) {
				image[yDisplay][xDisplay] = colours::grey;
			}
		}
	}

	PadLEDs::renderingLock = false;

	if (whichRowsCouldntBeRendered && image == PadLEDs::image) {
		uiNeedsRendering(this, whichRowsCouldntBeRendered, 0);
	}

	return true;
}

// Returns false if can't because in card routine
// occupancyMask can be NULL
bool ArrangerView::renderRow(ModelStack* modelStack, int32_t yDisplay, int32_t xScroll, uint32_t xZoom,
                             RGB* imageThisRow, uint8_t thisOccupancyMask[], int32_t renderWidth) {

	Output* output = outputsOnScreen[yDisplay];

	if (!output) {

		const RGB* imageEnd = imageThisRow + renderWidth;

		for (; imageThisRow < imageEnd; imageThisRow++) {
			*(imageThisRow) = colours::black;
		}

		// Occupancy mask doesn't need to be cleared in this case
		return true;
	}

	int32_t ignoreI = -2;
	bool drawGhostClipInstanceHere = false;
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW && !pressedClipInstanceIsInValidPosition) {
		if (yPressedEffective == yDisplay) {
			drawGhostClipInstanceHere = true;
		}
		if (output == pressedClipInstanceOutput) {
			ignoreI = pressedClipInstanceIndex;
		}
	}

	bool success =
	    renderRowForOutput(modelStack, output, xScroll, xZoom, imageThisRow, thisOccupancyMask, renderWidth, ignoreI);
	if (!success) {
		return false;
	}

	if (drawGhostClipInstanceHere) {

		int32_t xMovement =
		    currentSong->xScroll[NAVIGATION_ARRANGEMENT] - pressedClipInstanceXScrollWhenLastInValidPosition;
		ClipInstance* clipInstance = pressedClipInstanceOutput->clipInstances.getElement(pressedClipInstanceIndex);
		int32_t newStartPos = clipInstance->pos + xMovement;
		int32_t newEndPos = newStartPos + clipInstance->length;

		bool rightOnSquare;
		int32_t newStartSquare = getSquareFromPos(newStartPos, &rightOnSquare);
		int32_t newEndSquare = getSquareEndFromPos(newEndPos);

		newStartSquare = std::max(newStartSquare, 0_i32);
		newEndSquare = std::min(newEndSquare, renderWidth);

		if (blinkOn) {
			imageThisRow[newStartSquare] = clipInstance->getColour();
			int32_t lengthInSquares = newEndSquare - newStartSquare;
			if (lengthInSquares >= 2) {
				imageThisRow[newStartSquare + 3] = imageThisRow[newStartSquare].forTail();
			}
			for (int32_t x = newStartSquare + 2; x < newEndSquare; x++) {
				imageThisRow[x] = imageThisRow[newStartSquare + 3];
			}

			if (!rightOnSquare) {
				imageThisRow[newStartSquare] = imageThisRow[newStartSquare].forBlur();
			}
		}
		else {
			for (auto* it = &imageThisRow[newStartSquare];
			     it != &imageThisRow[newStartSquare] + (newEndSquare - newStartSquare); it++) {
				*it = colours::black;
			}
		}
	}

	return true;
}

// Lock rendering before calling this
// Returns false if can't because in card routine
// occupancyMask can be NULL
bool ArrangerView::renderRowForOutput(ModelStack* modelStack, Output* output, int32_t xScroll, uint32_t xZoom,
                                      RGB* image, uint8_t occupancyMask[], int32_t renderWidth, int32_t ignoreI) {

	RGB* imageNow = image;
	RGB* const imageEnd = image + renderWidth;

	// this is constant and zero - so does nothing?
	int32_t firstXDisplayNotLeftOf0 = 0;

	if (!output->clipInstances.getNumElements()) {
		while (imageNow < imageEnd) {
			*(imageNow++) = colours::black;
		}
		return true;
	}

	// the valies in searchTerms are always identical to the values in squareEndPos
	// how many of squareEndPos' values searchTerms holds is restricted by renderWidth
	int32_t squareEndPos[kMaxImageStoreWidth];
	int32_t searchTerms[kMaxImageStoreWidth];

	for (int32_t xDisplay = firstXDisplayNotLeftOf0; xDisplay < renderWidth; xDisplay++) {
		squareEndPos[xDisplay] = getPosFromSquare(xDisplay + 1, xScroll, xZoom);
	}

	memcpy(&searchTerms[firstXDisplayNotLeftOf0], &squareEndPos[firstXDisplayNotLeftOf0],
	       (renderWidth - firstXDisplayNotLeftOf0) * sizeof(int32_t));

	output->clipInstances.searchMultiple(&searchTerms[firstXDisplayNotLeftOf0], renderWidth - firstXDisplayNotLeftOf0);

	int32_t farLeftPos = getPosFromSquare(firstXDisplayNotLeftOf0, xScroll, xZoom);
	int32_t squareStartPos = farLeftPos;

	int32_t xDisplay = firstXDisplayNotLeftOf0;

	goto squareStartPosSet;

	do {
		squareStartPos = squareEndPos[xDisplay - 1];

squareStartPosSet:
		int32_t i = searchTerms[xDisplay] - 1; // Do "LESS"
		if (i == ignoreI) {
			i--;
		}
		ClipInstance* clipInstance = output->clipInstances.getElement(i);

		if (clipInstance) {
			RGB colour = clipInstance->getColour();

			// If Instance starts exactly on square or somewhere within square, draw "head".
			// We don't do the "blur" colour in arranger - it looks too white and would be confused with white/unique
			// instances

			// first square, draw head
			if (clipInstance->pos >= squareStartPos) {
				image[xDisplay] = colour;
			}
			// following squares
			else {
				// get the end of the clip instance
				int32_t instanceEnd = clipInstance->pos + clipInstance->length;
				// for currently recording clips, get the playhead
				if (output->recordingInArrangement && clipInstance->clip
				    && clipInstance->clip->getCurrentlyRecordingLinearly()) {
					instanceEnd = arrangement.getLivePos();
				}

				// if this clip goes beyond just the first square
				if (instanceEnd > squareStartPos) {

					// See how many squares long
					int32_t squareEnd = xDisplay;
					do {
						squareStartPos = squareEndPos[squareEnd];
						squareEnd++;
					} while (instanceEnd > squareStartPos && squareEnd < renderWidth
					         && searchTerms[squareEnd] - 1 == i);

					// Draw either the blank, non-existent Clip if this Instance doesn't have one...
					// Or the real Clip - for all squares in the Instance
					for (auto* it = &image[xDisplay]; it != &image[xDisplay] + (squareEnd - xDisplay); it++) {
						if (!clipInstance->clip) {
							// Arranger only clip instances created from a section clip (hold clip + turn Select) don't
							// have a clip until the pad is released. Their "clip preview" gets drawn here
							image[xDisplay] = colour.dim(4);
						}
						else {
							int32_t relativeSquarePos; // square's position relative to start of clip instance
							if (xDisplay == 0) {
								relativeSquarePos = (farLeftPos - clipInstance->pos);
							}
							else {
								relativeSquarePos = (squareEndPos[xDisplay - 1] - clipInstance->pos);
							}

							float multipleOfLoopLength = static_cast<float>(relativeSquarePos)
							                             / static_cast<float>(clipInstance->clip->loopLength);

							bool isLoopStart = std::floor(multipleOfLoopLength) == multipleOfLoopLength;

							if (isLoopStart) {
								if (clipInstance->clip->isArrangementOnlyClip()) {
									*it = (image[xDisplay] = colour.dim(2));
								}
								else {
									*it = (image[xDisplay] = colour.dim(3));
								}
							}
							else {
								if (clipInstance->clip->isArrangementOnlyClip()) {
									*it = (image[xDisplay] = colour.dim(4));
								}
								else {
									*it = (image[xDisplay] = colour.forBlur().dim(3));
								}
							}
						}
						xDisplay++;
					}

					xDisplay = squareEnd - 1;
				}
				// we haven't reached the next clipInstance yet
				else {
					image[xDisplay] = colours::black;
				}
			}
		}
		// no clipInstance
		else {
			image[xDisplay] = colours::black;
		}

		xDisplay++;
	} while (xDisplay < renderWidth);

	AudioEngine::logAction("Instrument::renderRow end");
	return true;
}

ActionResult ArrangerView::timerCallback() {
	switch (currentUIMode) {
	case UI_MODE_HOLDING_ARRANGEMENT_ROW:
		if (!pressedClipInstanceIsInValidPosition) {
			blinkOn = !blinkOn;

			uiNeedsRendering(this, 1 << yPressedEffective, 0);

			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kFastFlashTime);
		}
		break;

	case UI_MODE_NONE:
		if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			currentUIMode = UI_MODE_VIEWING_RECORD_ARMING;
			PadLEDs::reassessGreyout(false);
		case UI_MODE_VIEWING_RECORD_ARMING:
			// use root UI in case this is called from performance view
			requestRendering(getRootUI(), 0, 0xFFFFFFFF);
			blinkOn = !blinkOn;
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kFastFlashTime);
		}
		break;
	default:
		break;
	}

	return ActionResult::DEALT_WITH;
}

void ArrangerView::selectEncoderAction(int8_t offset) {

	Output* output = outputsOnScreen[yPressedEffective];

	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW) {

		actionOnDepress = false;

		if (!pressedClipInstanceIsInValidPosition) {
			return;
		}

		ClipInstance* clipInstance = output->clipInstances.getElement(pressedClipInstanceIndex);

		// If an arrangement-only Clip, can't do anything
		if (clipInstance->clip && clipInstance->clip->section == 255) {
			return;
		}

		// Picks next clip, scroll direction is passed by "offset"
		// Returns NULL at either end of the list of clips, which indicates the need for a white clip
		Clip* newClip = currentSong->getNextSessionClipWithOutput(offset, output, clipInstance->clip);

		// If no other Clips to switch to...
		if (newClip == clipInstance->clip) {
			return;
			// Looks like this never happens?
		}

		// Notify the arrangement that the existing clip will be removed
		if (clipInstance->clip) {
			arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length,
			                      clipInstance->clip, nullptr);
		}

		int32_t newLength;

		// No newClip means this will become a white clip
		if (!newClip) {
			// which will have the same length as the original clip instance
			newLength = desiredLength;
		}

		else {
			// choosing a section clip will reset the clip instance length to the length of that section clip
			newLength = newClip->loopLength;
		}

		// Make sure it's not too long
		ClipInstance* nextClipInstance = output->clipInstances.getElement(pressedClipInstanceIndex + 1);
		if (nextClipInstance) {
			int32_t maxLength = nextClipInstance->pos - clipInstance->pos;

			if (newLength > maxLength) {
				newLength = maxLength;
			}
		}
		if (newLength > kMaxSequenceLength - clipInstance->pos) {
			newLength = kMaxSequenceLength - clipInstance->pos;
		}
		// log action
		Action* action = actionLogger.getNewAction(ActionType::CLIP_INSTANCE_EDIT, ActionAddition::ALLOWED);
		clipInstance->change(action, output, clipInstance->pos, newLength, newClip);
		// notify the arrangement that this clip instance will be added
		arrangement.rowEdited(output, clipInstance->pos, clipInstance->pos + clipInstance->length, nullptr,
		                      clipInstance);

		interactWithClipInstance(output, yPressedEffective, clipInstance);

		uiNeedsRendering(this, 1 << yPressedEffective, 0);
	}

	else if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		navigateThroughPresets(offset);
	}

	else if (currentUIMode == UI_MODE_NONE) {

		if (playbackHandler.playbackState) {
			if (currentPlaybackMode == &session) {
				if (session.launchEventAtSwungTickCount && session.switchToArrangementAtLaunchEvent) {
					sessionView.editNumRepeatsTilLaunch(offset);
				}
			}

			else { // Arrangement playback
				if (offset == -1 && playbackHandler.stopOutputRecordingAtLoopEnd) {
					playbackHandler.stopOutputRecordingAtLoopEnd = false;
					renderDisplay();
				}
			}
		}
	}
}

void ArrangerView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {

	TimelineView::modEncoderAction(whichModEncoder, offset);
}

void ArrangerView::navigateThroughPresets(int32_t offset) {
	Output* output = outputsOnScreen[yPressedEffective];
	if (output->type == OutputType::AUDIO) {
		auto ao = (AudioOutput*)output;
		ao->scrollAudioOutputMode(offset);
		return;
	}

	endAudition(output);

	output = currentSong->navigateThroughPresetsForInstrument(output, offset);

	outputsOnScreen[yPressedEffective] = output;

	view.setActiveModControllableTimelineCounter(output->getActiveClip());

	AudioEngine::routineWithClusterLoading();

	beginAudition(output);
}

void ArrangerView::changeOutputType(OutputType newOutputType) {

	Output* output = outputsOnScreen[yPressedEffective];
	Instrument* oldInstrument = (Instrument*)output;
	OutputType oldOutputType = oldInstrument->type;

	if (oldOutputType == newOutputType) {
		return;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	endAudition(oldInstrument);

	Instrument* newInstrument = currentSong->changeOutputType(oldInstrument, newOutputType);
	if (!newInstrument) {
		return;
	}

	outputsOnScreen[yPressedEffective] = newInstrument;

	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	view.displayOutputName(newInstrument);
	view.setActiveModControllableTimelineCounter(newInstrument->getActiveClip());

	beginAudition(newInstrument);
}

void ArrangerView::changeOutputToAudio() {

	Output* oldOutput = outputsOnScreen[yPressedEffective];
	if (oldOutput->type == OutputType::AUDIO) {
		return;
	}

	if (oldOutput->clipInstances.getNumElements()) {
cant:
		display->displayPopup(deluge::l10n::get(
		    deluge::l10n::String::STRING_FOR_INSTRUMENTS_WITH_CLIPS_CANT_BE_TURNED_INTO_AUDIO_TRACKS));
		return;
	}

	InstrumentClip* instrumentClip = (InstrumentClip*)currentSong->getClipWithOutput(oldOutput);

	if (instrumentClip) {
		if (instrumentClip->containsAnyNotes()) {
			goto cant;
		}
		if (currentSong->getClipWithOutput(oldOutput, false, instrumentClip)) {
			goto cant; // Make sure not more than 1 Clip
		}

		// We'll do some other specific stuff below
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	endAudition(oldOutput);
	oldOutput->cutAllSound();

	AudioOutput* newOutput;
	Clip* newClip = nullptr;

	// If the old Output had a Clip that we're going to replace too...
	if (instrumentClip) {
		int32_t clipIndex = currentSong->sessionClips.getIndexForClip(instrumentClip);
		if (ALPHA_OR_BETA_VERSION && clipIndex == -1) {
			FREEZE_WITH_ERROR("E266");
		}
		newClip = currentSong->replaceInstrumentClipWithAudioClip(instrumentClip, clipIndex);

		if (!newClip) {
			display->displayError(Error::INSUFFICIENT_RAM);
			return;
		}

		newOutput = (AudioOutput*)newClip->output;
		currentSong->arrangementYScroll--;
	}

	// Or if no old Clip, we just simply make a new Output here and don't worry about Clips
	else {

		// Suss output
		newOutput = currentSong->createNewAudioOutput(oldOutput);
		if (!newOutput) {
			display->displayError(Error::INSUFFICIENT_RAM);
			return;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		newOutput->setupWithoutActiveClip(modelStack);
	}

	outputsOnScreen[yPressedEffective] = newOutput;

	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	view.displayOutputName(newOutput);
	view.setActiveModControllableTimelineCounter(newClip);
}

static const uint32_t horizontalEncoderScrollUIModes[] = {UI_MODE_HOLDING_ARRANGEMENT_ROW, 0};

ActionResult ArrangerView::horizontalEncoderAction(int32_t offset) {

	// Encoder button pressed...
	if (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {

		if (!Buttons::isShiftButtonPressed()) {

			int32_t oldXZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

			int32_t zoomMagnitude = -offset;

			// Constrain to zoom limits
			if (zoomMagnitude == -1) {
				if (oldXZoom <= 3) {
					return ActionResult::DEALT_WITH;
				}
				currentSong->xZoom[NAVIGATION_ARRANGEMENT] >>= 1;
			}
			else {
				if (oldXZoom >= getMaxZoom()) {
					return ActionResult::DEALT_WITH;
				}
				currentSong->xZoom[NAVIGATION_ARRANGEMENT] <<= 1;
			}

			int32_t oldScroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT];

			int32_t newScroll;
			uint32_t newZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

			if (arrangement.hasPlaybackActive() && doingAutoScrollNow) {
				int32_t actualCurrentPos = arrangement.getLivePos();
				int32_t howFarIn = actualCurrentPos - oldScroll;
				newScroll = actualCurrentPos - increaseMagnitude(howFarIn, zoomMagnitude);
				if (newScroll < 0) {
					newScroll = 0;
				}
			}
			else {
				newScroll = oldScroll;
			}

			int32_t screenWidth = newZoom * kDisplayWidth;
			if (newScroll > kMaxSequenceLength - screenWidth) {
				newScroll = kMaxSequenceLength - screenWidth;
			}

			newScroll = (uint32_t)(newScroll + (newZoom >> 1)) / newZoom * newZoom; // Rounding

			initiateXZoom(zoomMagnitude, newScroll, oldXZoom);
			displayZoomLevel();
		}
	}

	// Or shift presssed - extend or delete time
	else if (Buttons::isShiftButtonPressed()) {

		// Disallow while arranger playback active - we'd be battling autoscroll and stuff
		if (isNoUIModeActive()) {

			if (arrangement.hasPlaybackActive()) {
				indicator_leds::indicateAlertOnLed(IndicatorLED::PLAY);
			}
			else {

				int32_t scroll_amount = offset * currentSong->xZoom[NAVIGATION_ARRANGEMENT];

				// If expanding, make sure we don't exceed length limit
				if (offset >= 0 && static_cast<int32_t>(getMaxLength()) > kMaxSequenceLength - scroll_amount) {
					return ActionResult::DEALT_WITH;
				}

				ActionType actionType =
				    (offset >= 0) ? ActionType::ARRANGEMENT_TIME_EXPAND : ActionType::ARRANGEMENT_TIME_CONTRACT;

				Action* action = actionLogger.getNewAction(actionType, ActionAddition::ALLOWED);

				if (action
				    && (action->xScrollArranger[BEFORE] != currentSong->xScroll[NAVIGATION_ARRANGEMENT]
				        || action->xZoomArranger[BEFORE] != currentSong->xZoom[NAVIGATION_ARRANGEMENT])) {
					action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
				}

				// if this setting is on, shifting of automation is restricted to automation view
				if (!FlashStorage::automationShift) {
					ParamCollectionSummary* unpatchedParamsSummary =
					    currentSong->paramManager.getUnpatchedParamSetSummary();
					UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithParamCollection* modelStackWithUnpatchedParams =
					    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory)
					        ->addParamCollection(unpatchedParams, unpatchedParamsSummary);

					if (offset >= 0) {
						void* consMemory =
						    GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceArrangerParamsTimeInserted));
						if (consMemory) {
							ConsequenceArrangerParamsTimeInserted* consequence = new (consMemory)
							    ConsequenceArrangerParamsTimeInserted(currentSong->xScroll[NAVIGATION_ARRANGEMENT],
							                                          scroll_amount);
							action->addConsequence(consequence);
						}
						unpatchedParams->insertTime(modelStackWithUnpatchedParams,
						                            currentSong->xScroll[NAVIGATION_ARRANGEMENT], scroll_amount);
					}
					else {
						if (action) {
							unpatchedParams->backUpAllAutomatedParamsToAction(action, modelStackWithUnpatchedParams);
						}
						unpatchedParams->deleteTime(modelStackWithUnpatchedParams,
						                            currentSong->xScroll[NAVIGATION_ARRANGEMENT], -scroll_amount);
					}
				}

				for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
					int32_t i = thisOutput->clipInstances.search(currentSong->xScroll[NAVIGATION_ARRANGEMENT],
					                                             GREATER_OR_EQUAL);

					bool movedOneYet = false;

					// And move the successive ones.
					while (i < thisOutput->clipInstances.getNumElements()) {
						ClipInstance* instance = thisOutput->clipInstances.getElement(i);

						// If contracting time and this bit has to be deleted...
						if (offset < 0
						    && instance->pos + scroll_amount < currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {
							deleteClipInstance(thisOutput, i, instance, action);
							// Don't increment i, because we deleted an element
						}

						// Otherwise, just move it
						else {

							int32_t new_pos = instance->pos + scroll_amount;

							// If contracting time, shorten the previous ClipInstance only if the ClipInstances we're
							// moving will eat into its tail. Otherwise, leave the tail there. Perhaps it'd make more
							// sense to cut the tail off regardless, but possibly just due to me not thinking about it,
							// this was not done in pre-V4 firmware, and actually having it this way probably helps
							// users.
							if (!movedOneYet && offset < 0 && i > 0) {
								movedOneYet = true;
								ClipInstance* prevInstance = (ClipInstance*)thisOutput->clipInstances.getElement(i - 1);
								int32_t maxLength = new_pos - prevInstance->pos;
								if (prevInstance->length > maxLength) {
									prevInstance->change(action, thisOutput, prevInstance->pos, maxLength,
									                     prevInstance->clip);
								}
							}

							instance->change(action, thisOutput, new_pos, instance->length, instance->clip);

							i++;
						}
					}
				}

				lastInteractedPos += scroll_amount;

				uiNeedsRendering(this, 0xFFFFFFFF, 0);
			}
		}
	}

	// Encoder button not pressed - we'll just scroll (and possibly drag a ClipInstance horizontally)
	else if (isUIModeWithinRange(horizontalEncoderScrollUIModes)) {

		actionOnDepress = false;

		// When dragging a clip instance, allow temporary negative scroll to continue leftward movement
		// but stop it once the clip instance is at 0
		bool can_drag_clip_instance = false;

		if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW)) {

			ClipInstance* pressed_instance =
			    (ClipInstance*)pressedClipInstanceOutput->clipInstances.getElement(pressedClipInstanceIndex);
			int32_t xMovement =
			    currentSong->xScroll[NAVIGATION_ARRANGEMENT] - pressedClipInstanceXScrollWhenLastInValidPosition;
			int32_t dragged_clip_position = pressed_instance->pos + xMovement;
			can_drag_clip_instance = (dragged_clip_position > 0);
		}

		if (can_drag_clip_instance || currentSong->xScroll[NAVIGATION_ARRANGEMENT] > 0 || offset == 1) {
			// Allow movement left if we are above zero when either dragging or scrolling.
			// This will always stop scrolling at zero, but dragging might let the clip instance go one
			// square below zero if it is for example at a half step offset. But it will blink and then snap back to
			// the last valid square (with the offset preserved), so that lets you know it's not exactly at zero.
			// Also allows movement right so we don't get stuck at zero. The right side limit is in the function.
			return horizontalScrollOneSquare(offset);
		}
		return ActionResult::DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

ActionResult ArrangerView::horizontalScrollOneSquare(int32_t direction) {
	actionOnDepress = false;

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	int32_t scroll_amount = direction * static_cast<int32_t>(xZoom);

	bool dragging_clip_instance = isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW);

	// Only apply normal scroll constraints when not dragging to allow temporary negative
	// window position so that clip instances can be dragged all the way to the start
	if (!dragging_clip_instance) {
		scroll_amount = std::max(scroll_amount, -currentSong->xScroll[NAVIGATION_ARRANGEMENT]);
	}

	// Calculate max scroll bounds
	int32_t max_scroll = static_cast<int32_t>(getMaxLength()) - 1 + static_cast<int32_t>(xZoom);
	int32_t screen_width = static_cast<int32_t>(xZoom) << kDisplayWidthMagnitude;
	max_scroll = std::clamp(max_scroll, int32_t{0}, kMaxSequenceLength - screen_width);

	int32_t new_x_scroll = currentSong->xScroll[NAVIGATION_ARRANGEMENT] + scroll_amount;

	// Apply maximum scroll limit
	if (new_x_scroll > max_scroll) {
		new_x_scroll = (max_scroll / static_cast<int32_t>(xZoom)) * static_cast<int32_t>(xZoom);
	}

	if (new_x_scroll != currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {

		if (dragging_clip_instance && sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		currentSong->xScroll[NAVIGATION_ARRANGEMENT] = new_x_scroll;

		if (dragging_clip_instance) {
			// might have to get shifted left or right to a valid position
			putDraggedClipInstanceInNewPosition(outputsOnScreen[yPressedEffective]);
		}

		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		reassessWhetherDoingAutoScroll();
	}

	// Display scroll position, but handle potential negative scroll values during dragging
	if (dragging_clip_instance && currentSong->xScroll[NAVIGATION_ARRANGEMENT] < 0) {
		// It can't handle negative values, so just display the 0 position (1:1:1)
		displayNumberOfBarsAndBeats(0, kDisplayWidth, true, "");
	}
	else {
		displayScrollPos();
	}

	return ActionResult::DEALT_WITH;
}

// No need to check whether playback active before calling - we check for that here.
void ArrangerView::reassessWhetherDoingAutoScroll(int32_t pos) {

	doingAutoScrollNow = false;

	if (!currentSong->arrangerAutoScrollModeActive || !arrangement.hasPlaybackActive()) {
		return;
	}

	if (pos == -1) {
		pos = arrangement.getLivePos();
	}
	doingAutoScrollNow = (pos >= getPosFromSquare(0) && pos < getPosFromSquare(kDisplayWidth));

	if (doingAutoScrollNow) {
		autoScrollNumSquaresBehind = getSquareFromPos(pos);
	}
}

ActionResult ArrangerView::verticalScrollOneSquare(int32_t direction) {
	if (direction >= 0) { // Up
		if (currentSong->arrangementYScroll >= currentSong->getNumOutputs() - 1) {
			return ActionResult::DEALT_WITH;
		}
	}
	else { // Down
		if (currentSong->arrangementYScroll <= 1 - kDisplayHeight) {
			return ActionResult::DEALT_WITH;
		}
	}

	Output* output;

	// prevent dragging clip instance vertically, since it won't work anyways
	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW)) {
		return ActionResult::DEALT_WITH;
	}

	// If an output is selected for dragging by holding the audition pad,
	// limit scrolling to within bounds of arrangement rows
	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		if (yPressedEffective != yPressedActual) {
			return ActionResult::DEALT_WITH;
		}

		output = outputsOnScreen[yPressedEffective];

		if (direction >= 0) { // Up
			if (output->next == nullptr) {
				return ActionResult::DEALT_WITH;
			}
		}
		else { // Down
			if (currentSong->firstOutput == output) {
				return ActionResult::DEALT_WITH;
			}
		}

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		actionLogger.deleteAllLogs();

		currentSong->arrangementYScroll += direction;

		// Drag selected output against the scroll
		// Shift Output up
		if (direction >= 0) {
			Output** prevPointer = &currentSong->firstOutput;
			while (*prevPointer != output) {
				prevPointer = &((*prevPointer)->next);
			}
			Output* higher = output->next;
			*prevPointer = higher;
			output->next = higher->next;
			higher->next = output;
		}

		// Shift Output down
		else {
			Output** prevPointer = &currentSong->firstOutput;
			while ((*prevPointer)->next != output) {
				prevPointer = &((*prevPointer)->next);
			}
			Output* lower = (*prevPointer);
			*prevPointer = output;
			lower->next = output->next;
			output->next = lower;
		}
	}
	else {
		currentSong->arrangementYScroll += direction;
	}

	repopulateOutputsOnScreen();

	if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
		PadLEDs::reassessGreyout(true);
	}

	return ActionResult::DEALT_WITH;
}

static const uint32_t verticalEncoderUIModes[] = {UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
                                                  UI_MODE_HOLDING_ARRANGEMENT_ROW, UI_MODE_VIEWING_RECORD_ARMING, 0};

ActionResult ArrangerView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {

	if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
		if (currentUIMode == UI_MODE_NONE) {
			currentSong->commandTranspose(offset);
		}
	}
	else if (isUIModeWithinRange(verticalEncoderUIModes)) {
		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
		}

		return verticalScrollOneSquare(offset);
	}

	return ActionResult::DEALT_WITH;
}

void ArrangerView::setNoSubMode() {
	// If we were dragging a clip instance left and have a negative scroll position, snap back to 0
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW && currentSong->xScroll[NAVIGATION_ARRANGEMENT] < 0) {
		currentSong->xScroll[NAVIGATION_ARRANGEMENT] = 0;
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}

	currentUIMode = UI_MODE_NONE;
	if (doingAutoScrollNow) {
		reassessWhetherDoingAutoScroll(); // Maybe stop auto-scrolling. But don't start.
	}
	setLedStates();
}

static const uint32_t autoScrollUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
                                             UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION, UI_MODE_HORIZONTAL_ZOOM,
                                             UI_MODE_STEM_EXPORT, 0};

void ArrangerView::graphicsRoutine() {
	UI* ui = getCurrentUI();

	sessionView.potentiallyUpdateCompressorLEDs();

	if (view.potentiallyRenderVUMeter(PadLEDs::image)) {
		PadLEDs::sendOutSidebarColours();
	}

	// Request OLED refresh for visualizer if active (ensures continuous updates)
	deluge::hid::display::Visualizer::requestVisualizerUpdateIfNeeded();

	if (display->haveOLED()) {
		sessionView.displayPotentialTempoChange(this);
	}

	if (PadLEDs::flashCursor != FLASH_CURSOR_OFF) {

		int32_t newTickSquare;

		if (!arrangement.hasPlaybackActive() || currentUIMode == UI_MODE_EXPLODE_ANIMATION
		    || currentUIMode == UI_MODE_IMPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn) {
			newTickSquare = 255;
		}
		else {
			int32_t actualCurrentPos = arrangement.getLivePos();

			// If doing auto scroll...
			if (doingAutoScrollNow && isUIModeWithinRange(autoScrollUIModes)) {

				int32_t newScrollPos =
				    (actualCurrentPos / currentSong->xZoom[NAVIGATION_ARRANGEMENT] - autoScrollNumSquaresBehind)
				    * currentSong->xZoom[NAVIGATION_ARRANGEMENT];

				// If now is the time to scroll to a different position (usually one square)...
				if (newScrollPos != currentSong->xScroll[NAVIGATION_ARRANGEMENT]) {
					bool wasLessThanZero = (newScrollPos < 0);
					if (wasLessThanZero) {
						newScrollPos = 0;
					}

					currentSong->xScroll[NAVIGATION_ARRANGEMENT] = newScrollPos;
					if (wasLessThanZero) {
						autoScrollNumSquaresBehind = getSquareFromPos(actualCurrentPos);
					}

					if (PadLEDs::flashCursor == FLASH_CURSOR_FAST) {
						PadLEDs::clearTickSquares();  // Make sure new fast flashes get sent out
						mustRedrawTickSquares = true; // Make sure this gets sent below here
					}
					if (currentUIMode != UI_MODE_HORIZONTAL_ZOOM) {
						uiNeedsRendering(ui, 0xFFFFFFFF, 0);
					}
				}
			}

			newTickSquare = getSquareFromPos(actualCurrentPos);

			if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
				doingAutoScrollNow = false;
			}
		}

		// If tick square changed (or we decided it has to be redrawn anyway)...
		if (newTickSquare != lastTickSquare || mustRedrawTickSquares) {

			uint8_t tickSquares[kDisplayHeight];
			uint8_t colours[kDisplayHeight];

			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				Output* output = outputsOnScreen[yDisplay];
				tickSquares[yDisplay] =
				    (currentSong->getAnyOutputsSoloingInArrangement() && (!output || !output->soloingInArrangementMode))
				        ? 255
				        : newTickSquare;
				colours[yDisplay] =
				    (output && output->recordingInArrangement) ? 2 : (output && output->mutedInArrangementMode ? 1 : 0);

				if (arrangement.hasPlaybackActive() && currentUIMode != UI_MODE_EXPLODE_ANIMATION
				    && currentUIMode != UI_MODE_IMPLODE_ANIMATION) {

					// If linear recording to this Output, re-render it
					if (output->recordingInArrangement) {
						uiNeedsRendering(ui, (1 << yDisplay), 0);
					}
				}
			}

			PadLEDs::setTickSquares(tickSquares, colours);
			lastTickSquare = newTickSquare;
		}
	}

	mustRedrawTickSquares = false;
}

void ArrangerView::notifyActiveClipChangedOnOutput(Output* output) {
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION && outputsOnScreen[yPressedEffective] == output) {
		view.setActiveModControllableTimelineCounter(output->getActiveClip());
	}
}

static const uint32_t autoScrollPlaybackEndUIModes[] = {UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
                                                        UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

void ArrangerView::autoScrollOnPlaybackEnd() {

	if (doingAutoScrollNow && isUIModeWithinRange(autoScrollPlaybackEndUIModes)
	    && !Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		// Don't do it if they're instantly restarting playback again

		uint32_t xZoom = currentSong->xZoom[NAVIGATION_ARRANGEMENT];
		int32_t newScrollPos =
		    ((uint32_t)(arrangement.playbackStartedAtPos + (xZoom >> 1)) / xZoom - autoScrollNumSquaresBehind) * xZoom;

		if (newScrollPos < 0) {
			newScrollPos = 0;
		}

		// If that actually puts us back to near where we were scrolled to when playback began (which it usually will),
		// just go back there exactly. Added in response to Michael noting that if you do an UNDO and then also stop
		// playback while recording e.g. MIDI to arranger, it scrolls backwards twice (if you have "follow" on).
		// Actually it seems that in that situation, undoing (probably due to other mechanics that get enacted) won't
		// let it take you further than 1 screen back from the play-cursor
		// - which just means that this is "extra" effective I guess.
		if (newScrollPos > xScrollWhenPlaybackStarted - (xZoom >> kDisplayWidthMagnitude)
		    || newScrollPos < xScrollWhenPlaybackStarted + (xZoom >> kDisplayWidthMagnitude)) {
			newScrollPos = xScrollWhenPlaybackStarted;
		}

		int32_t scrollDifference = newScrollPos - currentSong->xScroll[NAVIGATION_ARRANGEMENT];

		if (scrollDifference) {

			// If allowed to do a nice scrolling animation...
			if (currentUIMode == UI_MODE_NONE && getCurrentUI() == this && !PadLEDs::renderingLock) {
				bool worthDoingAnimation = initiateXScroll(newScrollPos);
				if (!worthDoingAnimation) {
					goto sharpJump;
				}
			}

			// Otherwise, just jump sharply
			else {
sharpJump:
				currentSong->xScroll[NAVIGATION_ARRANGEMENT] = newScrollPos;
				uiNeedsRendering(this, 0xFFFFFFFF, 0);
			}
		}
	}
}

// Returns false if too few squares to bother with animation
bool ArrangerView::initiateXScroll(int32_t newScrollPos) {
	int32_t distanceToScroll = newScrollPos - currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	if (distanceToScroll < 0) {
		distanceToScroll = -distanceToScroll;
	}
	int32_t squaresToScroll = distanceToScroll / currentSong->xZoom[NAVIGATION_ARRANGEMENT];
	if (squaresToScroll <= 1) {
		return false;
	}
	if (squaresToScroll > kDisplayWidth) {
		squaresToScroll = kDisplayWidth;
	}
	TimelineView::initiateXScroll(newScrollPos, squaresToScroll);

	return true;
}

uint32_t ArrangerView::getMaxLength() {

	uint32_t maxEndPos = 0;
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		if (thisOutput->recordingInArrangement) {
			int32_t livePos = arrangement.getLivePos();
			if (livePos > 0) { // Only consider positive positions
				maxEndPos = std::max(maxEndPos, static_cast<uint32_t>(livePos));
			}
		}

		int32_t numElements = thisOutput->clipInstances.getNumElements();
		if (numElements) {
			ClipInstance* lastInstance = thisOutput->clipInstances.getElement(numElements - 1);
			uint32_t endPos = static_cast<uint32_t>(lastInstance->pos + lastInstance->length);
			maxEndPos = std::max(maxEndPos, endPos);
		}
	}

	return maxEndPos;
}

uint32_t ArrangerView::getMaxZoom() {
	uint32_t max_length = getMaxLength();

	if (max_length < (kDefaultArrangerZoom << currentSong->insideWorldTickMagnitude) * kDisplayWidth) {
		return (kDefaultArrangerZoom << currentSong->insideWorldTickMagnitude);
	}

	uint32_t this_length = kDisplayWidth * 3;
	while (this_length < max_length) {
		this_length <<= 1;
	}

	if (this_length < (kMaxSequenceLength >> 1)) {
		this_length <<= 1;
	}

	int32_t max_zoom = this_length >> kDisplayWidthMagnitude;

	return max_zoom;
}

void ArrangerView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		PadLEDs::transitionTakingPlaceOnRow[yDisplay] =
		    (outputsOnScreen[yDisplay] && outputsOnScreen[yDisplay]->clipInstances.getNumElements());
	}
}

void ArrangerView::scrollFinished() {
	TimelineView::scrollFinished();
	reassessWhetherDoingAutoScroll();
}

void ArrangerView::notifyPlaybackBegun() {
	mustRedrawTickSquares = true;
	if (currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		endAudition(outputsOnScreen[yPressedEffective], true);
	}
}

bool ArrangerView::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
		*cols = 0xFFFFFFFD;
		*rows = 0;
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			if (outputsOnScreen[yDisplay] && !outputsOnScreen[yDisplay]->armedForRecording) {
				*rows |= (1 << yDisplay);
			}
		}
		return true;
	}
	else {
		return false;
	}
}

uint32_t ArrangerView::getGreyedOutRowsNotRepresentingOutput(Output* output) {
	uint32_t rows = 0xFFFFFFFF;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (outputsOnScreen[yDisplay] == output) {
			rows &= ~(1 << yDisplay);
			break;
		}
	}
	return rows;
}

void ArrangerView::playbackEnded() {
	if (currentPlaybackMode == &arrangement) {
		autoScrollOnPlaybackEnd();
	}

	if (getCurrentUI() == &arrangerView) { // Why do we need to check this?
		renderDisplay();
	}
}

void ArrangerView::clipNeedsReRendering(Clip* clip) {

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Output* output = outputsOnScreen[yDisplay];
		if (output == clip->output) {
			// In a perfect world we'd see if the Clip is actually horizontally scrolled on-screen
			uiNeedsRendering(this, (1 << yDisplay), 0);
			break;
		}
	}
}

Clip* ArrangerView::getClipForSelection() {
	Clip* clip = nullptr;
	// if you're in arranger view, check if you're pressing a clip or holding audition pad to control that clip
	if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW) && lastInteractedClipInstance) {
		clip = lastInteractedClipInstance->clip;
	}
	else if (isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		Output* output = outputsOnScreen[yPressedEffective];
		clip = currentSong->getClipWithOutput(output);
	}
	return clip;
}

void ArrangerView::requestRendering(UI* ui, uint32_t whichMainRows, uint32_t whichSideRows) {
	if (ui == &performanceView) {
		// don't re-render main pads in performance view
		uiNeedsRendering(ui, 0, whichSideRows);
	}
	else if (ui == &arrangerView) {
		uiNeedsRendering(ui, whichMainRows, whichSideRows);
	}
}
