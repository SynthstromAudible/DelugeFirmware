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

#include "gui/views/arranger_view.h"
#include "processing/engines/audio_engine.h"
#include "gui/context_menu/clear_song.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/ui/root_ui.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "gui/views/session_view.h"
#include "model/timeline_counter.h"
#include "gui/views/view.h"
#include "definitions.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/keyboard_screen.h"
#include "model/instrument/instrument.h"
#include "io/midi/midi_engine.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "model/drum/drum.h"
#include "model/instrument/melodic_instrument.h"
#include "playback/mode/arrangement.h"
#include "io/uart/uart.h"
#include "model/instrument/midi_instrument.h"
#include "model/drum/kit.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "hid/encoders.h"
#include "gui/ui/save/save_song_ui.h"
#include "model/note/note_row.h"
#include "model/clip/audio_clip.h"
#include "playback/mode/session.h"
#include "hid/led/indicator_leds.h"
#include "storage/flash_storage.h"
#include "hid/buttons.h"
#include "processing/audio_output.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"
#include "gui/menu_item/colour.h"
#include "extern.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_device.h"
#include "io/midi/learned_midi.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound_instrument.h"
#include "storage/file_item.h"
#include "gui/ui_timer_manager.h"
#include "gui/ui/load/load_song_ui.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

View view{};

extern int8_t pendingGlobalMIDICommand;

View::View() {
	midiLearnFlashOn = false;

	thingPressedForMidiLearn = 0;
	deleteMidiCommandOnRelease = false;

	learnedThing = NULL;

	memset(&activeModControllableModelStack, 0, sizeof(activeModControllableModelStack));

	modLength = 0;
	modPos = 0xFFFFFFFF;
	clipArmFlashOn = false;
}

void View::focusRegained() {
	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	setTripletsLedState();

	indicator_leds::setLedState(IndicatorLED::LOAD, false);
	indicator_leds::setLedState(IndicatorLED::SAVE, false);

	indicator_leds::setLedState(IndicatorLED::LEARN, false);
}

void View::setTripletsLedState() {
	RootUI* rootUI = getRootUI();

	indicator_leds::setLedState(IndicatorLED::TRIPLETS,
	                            rootUI->isTimelineView() && ((TimelineView*)rootUI)->inTripletsView());
}

extern int pendingGlobalMIDICommandNumClustersWritten;

int View::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	int newGlobalMidiCommand;

	// Tap tempo button. Shouldn't move this to MatrixDriver, because this code can put us in tapTempo mode, and other UIs aren't built to
	// handle this
	if (b == TAP_TEMPO) {

		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (on) {
				deleteMidiCommandOnRelease = true;
				endMidiLearnPressSession(MIDI_LEARN_TAP_TEMPO_BUTTON);
				learnedThing = &midiEngine.globalMIDICommands[GLOBAL_MIDI_COMMAND_TAP];
			}

			else if (thingPressedForMidiLearn == MIDI_LEARN_TAP_TEMPO_BUTTON) {
doEndMidiLearnPressSession:
				if (deleteMidiCommandOnRelease) {
					learnedThing->clear();
					shouldSaveSettingsAfterMidiLearn = true;
				}
				endMidiLearnPressSession(0);
			}
		}
		else if (currentUIMode == UI_MODE_NONE) {
			if (on) {
				// If shift button, toggle metronome
				if (Buttons::isShiftButtonPressed()) {
					playbackHandler.toggleMetronomeStatus();
				}
				// Otherwise, normal - tap tempo
				else {
					playbackHandler.tapTempoButtonPress();
				}
			}
		}
	}

	// MIDI learn button
	else if (b == LEARN) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN) {
				thingPressedForMidiLearn = 0;
				shouldSaveSettingsAfterMidiLearn = false;
				currentUIMode = UI_MODE_MIDI_LEARN;
				midiLearnFlash();
				indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1);
			}
		}
		else {
			if (currentUIMode == UI_MODE_MIDI_LEARN) {
				endMIDILearn();
			}
		}
	}

	// Play button for MIDI learn
	else if (b == PLAY && currentUIMode == UI_MODE_MIDI_LEARN) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			deleteMidiCommandOnRelease = true;
			endMidiLearnPressSession(MIDI_LEARN_PLAY_BUTTON);
			learnedThing = &midiEngine.globalMIDICommands[GLOBAL_MIDI_COMMAND_PLAY];
		}
		else if (thingPressedForMidiLearn == MIDI_LEARN_PLAY_BUTTON) {
			goto doEndMidiLearnPressSession;
		}
	}

	// Record button for MIDI learn
	else if (b == RECORD && currentUIMode == UI_MODE_MIDI_LEARN) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			deleteMidiCommandOnRelease = true;
			endMidiLearnPressSession(MIDI_LEARN_RECORD_BUTTON);
			learnedThing = &midiEngine.globalMIDICommands[GLOBAL_MIDI_COMMAND_RECORD];
		}
		else if (thingPressedForMidiLearn == MIDI_LEARN_RECORD_BUTTON) {
			goto doEndMidiLearnPressSession;
		}
	}

	// Save button
	else if (b == SAVE) {

		if (!Buttons::isButtonPressed(hid::button::SYNTH) && !Buttons::isButtonPressed(hid::button::KIT)
		    && !Buttons::isButtonPressed(hid::button::MIDI) && !Buttons::isButtonPressed(hid::button::CV)) {
			// Press down
			if (on) {
				if (currentUIMode == UI_MODE_NONE && !Buttons::isShiftButtonPressed()) {
					currentUIMode = UI_MODE_HOLDING_SAVE_BUTTON;
					timeSaveButtonPressed = AudioEngine::audioSampleTimer;
					indicator_leds::setLedState(IndicatorLED::SAVE, true);
				}
			}

			// Press release
			else {
				if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON) {
					if (inCardRoutine) {
						return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					currentUIMode = UI_MODE_NONE;

					if ((int32_t)(AudioEngine::audioSampleTimer - timeSaveButtonPressed) < (44100 >> 1)) {
						if (currentSong->hasAnyPendingNextOverdubs()) {
							numericDriver.displayPopup(HAVE_OLED ? "Can't save while overdubs pending" : "CANT");
						}
						else {
							openUI(&saveSongUI);
						}
					}
					else {
						indicator_leds::setLedState(IndicatorLED::SAVE, false);
					}
				}
			}
		}
	}

	// Load button
	else if (b == LOAD) {

		if (!Buttons::isButtonPressed(hid::button::SYNTH) && !Buttons::isButtonPressed(hid::button::KIT)
		    && !Buttons::isButtonPressed(hid::button::MIDI) && !Buttons::isButtonPressed(hid::button::CV)) {
			// Press down
			if (on) {
				if (currentUIMode == UI_MODE_NONE) {

					if (Buttons::isShiftButtonPressed()) {
						if (inCardRoutine) {
							return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}
						bool available = context_menu::clearSong.setupAndCheckAvailability();
						if (available) {
							openUI(&context_menu::clearSong);
						}
					}

					else {
						currentUIMode = UI_MODE_HOLDING_LOAD_BUTTON;
						timeSaveButtonPressed = AudioEngine::audioSampleTimer;
						indicator_leds::setLedState(IndicatorLED::LOAD, true);
					}
				}
			}

			// Press release
			else {
				if (currentUIMode == UI_MODE_HOLDING_LOAD_BUTTON) {
					if (inCardRoutine) {
						return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}
					currentUIMode = UI_MODE_NONE;

					if ((int32_t)(AudioEngine::audioSampleTimer - timeSaveButtonPressed) < (44100 >> 1)) {
						bool success = openUI(&loadSongUI);

						// Need to redraw everything if no success, because the LoadSongUI does some drawing before even determining whether it can start successfully
						if (!success) {
							//((ViewScreen*)getCurrentUI())->renderAllRows();
							//beginSession();
						}
					}
					else {
						indicator_leds::setLedState(IndicatorLED::LOAD, false);
					}
				}
			}
		}
	}

	// Sync-scaling button
	else if (b == SYNC_SCALING) {
		if (on && currentUIMode == UI_MODE_NONE) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
cant:
				numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
				return ACTION_RESULT_DEALT_WITH;
			}

			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// If no scaling currently, start it, if we're on a Clip-minder screen
			if (!currentSong->getSyncScalingClip()) {
				if (!getCurrentUI()->toClipMinder()) {
					indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW);
					return ACTION_RESULT_DEALT_WITH;
				}

				// Can't do it for arranger-only Clips
				if (currentSong->currentClip->isArrangementOnlyClip()) {
					goto cant;
				}

				// Can't do it for Clips recording linearly
				if (currentSong->currentClip->getCurrentlyRecordingLinearly()) {
					goto cant;
				}

				currentSong->setInputTickScaleClip(currentSong->currentClip);
			}

			// Or if scaling already, stop it
			else {
				currentSong->setInputTickScaleClip(NULL);
			}

			actionLogger.deleteAllLogs(); // Can't undo past this.

			playbackHandler.resyncInternalTicksToInputTicks(currentSong);
			setTimeBaseScaleLedState();
		}
	}

	// Back button
	else if (b == BACK) {

		if (on) {
#ifndef undoButtonX
			// Undo / redo
			if (actionLogger.allowedToDoReversion()) {

				// Here we'll take advantage of the pending command system which has to exist for these commands for their MIDI-triggered case anyway.
				// In the future, maybe a lot more commands should pend in the same way?
				pendingGlobalMIDICommand =
				    Buttons::isShiftButtonPressed() ? GLOBAL_MIDI_COMMAND_REDO : GLOBAL_MIDI_COMMAND_UNDO;
				pendingGlobalMIDICommandNumClustersWritten = 0; // Bug hunting.
				playbackHandler.slowRoutine();                  // Do it now if not reading card.
			}

			else
#endif
				// If we were in tap tempo mode (i.e. waiting for the second tap), stop it!
				if (currentUIMode == UI_MODE_TAP_TEMPO) {
					playbackHandler.tapTempoAutoSwitchOff();
				}
		}
	}

#ifdef undoButtonX
	// Undo button
	else if (b == undo) {
		newGlobalMidiCommand = GLOBAL_MIDI_COMMAND_UNDO;
possiblyRevert:
		if (on) {
			if (actionLogger.allowedToDoReversion()) {

				// Here we'll take advantage of the pending command system which has to exist for these commands for their MIDI-triggered case anyway.
				// In the future, maybe a lot more commands should pend in the same way?
				pendingGlobalMIDICommand = newGlobalMidiCommand;
				pendingGlobalMIDICommandNumClustersWritten = 0;
				playbackHandler.slowRoutine(); // Do it now if not reading card
			}
		}
	}

	// Redo button
	else if (b == redo) {
		newGlobalMidiCommand = GLOBAL_MIDI_COMMAND_REDO;
		goto possiblyRevert;
	}
#endif

	// Select button with shift - go to settings menu
	else if (b == SELECT_ENC && Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
				return ACTION_RESULT_DEALT_WITH;
			}

			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			numericDriver.setNextTransitionDirection(1);
			soundEditor.setup();
			openUI(&soundEditor);
		}
	}
	else {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void View::endMIDILearn() {
	if (shouldSaveSettingsAfterMidiLearn) {
		if (!AudioEngine::audioRoutineLocked) {
			FlashStorage::writeSettings(); // Rare case where we could have been called during audio routine
		}
	}
	uiTimerManager.unsetTimer(TIMER_MIDI_LEARN_FLASH);
	midiLearnFlashOn = false;
	if (getRootUI()) {
		getRootUI()->midiLearnFlash();
	}
	currentUIMode = UI_MODE_NONE;
	playbackHandler.setLedStates();
	indicator_leds::setLedState(IndicatorLED::LEARN, false);
}

void View::setTimeBaseScaleLedState() {
	// If this Clip is the inputTickScaleClip, flash the LED
	if (getCurrentUI()->toClipMinder() && currentSong->currentClip == currentSong->getSyncScalingClip()) {
		indicator_leds::blinkLed(IndicatorLED::SYNC_SCALING);
	}

	// Otherwise, just light it solidly on or off
	else {
		indicator_leds::setLedState(IndicatorLED::SYNC_SCALING, currentSong->getSyncScalingClip() != NULL);
	}
}

void View::setLedStates() {
	setTimeBaseScaleLedState();
}

void View::sectionMidiLearnPadPressed(bool on, uint8_t section) {
	if (on) {
		endMidiLearnPressSession(MIDI_LEARN_SECTION);
		deleteMidiCommandOnRelease = true;
		learnedThing = &currentSong->sections[section].launchMIDICommand;
	}
	else if (thingPressedForMidiLearn == MIDI_LEARN_SECTION) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
		}
		endMidiLearnPressSession(0);
	}
}

void View::clipStatusMidiLearnPadPressed(bool on, Clip* whichClip) {
	if (on) {
		endMidiLearnPressSession(MIDI_LEARN_CLIP);
		deleteMidiCommandOnRelease = true;
		learnedThing = &whichClip->muteMIDICommand;
	}
	else if (thingPressedForMidiLearn == MIDI_LEARN_CLIP) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
		}
		endMidiLearnPressSession(0);
	}
}

void View::noteRowMuteMidiLearnPadPressed(bool on, NoteRow* whichNoteRow) {
	if (on) {
		endMidiLearnPressSession(MIDI_LEARN_NOTEROW_MUTE);
		deleteMidiCommandOnRelease = true;
		learnedThing = &whichNoteRow->drum->muteMIDICommand;
	}
	else if (thingPressedForMidiLearn == MIDI_LEARN_NOTEROW_MUTE) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
		}
		endMidiLearnPressSession(0);
	}
}

void View::drumMidiLearnPadPressed(bool on, Drum* drum, Kit* kit) {
	if (on) {
		endMidiLearnPressSession(MIDI_LEARN_DRUM_INPUT);
		deleteMidiCommandOnRelease = true;
		learnedThing = &drum->midiInput;
		drumPressedForMIDILearn = drum;
		kitPressedForMIDILearn =
		    kit; // Having this makes it possible to search much faster when we call grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum()
	}

	else if (thingPressedForMidiLearn == MIDI_LEARN_DRUM_INPUT) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
			((Instrument*)currentSong->currentClip->output)->beenEdited(false);
		}
		endMidiLearnPressSession(0);
	}
}

void View::melodicInstrumentMidiLearnPadPressed(bool on, MelodicInstrument* instrument) {
	if (on) {
		endMidiLearnPressSession(MIDI_LEARN_MELODIC_INSTRUMENT_INPUT);
		deleteMidiCommandOnRelease = true;
		learnedThing = &instrument->midiInput;
		melodicInstrumentPressedForMIDILearn = instrument;
		highestMIDIChannelSeenWhileLearning = -1;
		lowestMIDIChannelSeenWhileLearning = 16;
	}

	else if (thingPressedForMidiLearn == MIDI_LEARN_MELODIC_INSTRUMENT_INPUT) {
		if (deleteMidiCommandOnRelease) {
			clearMelodicInstrumentMonoExpressionIfPossible(); // In case it gets "stuck".
			learnedThing->clear();
			instrument->beenEdited(false);
		}
		endMidiLearnPressSession(0);
	}
}

void View::endMidiLearnPressSession(uint8_t newThingPressed) {
	// Depending on which thing was previously pressed, we might have to do some admin
	switch (thingPressedForMidiLearn) {
	case MIDI_LEARN_PLAY_BUTTON:
	case MIDI_LEARN_RECORD_BUTTON:
	case MIDI_LEARN_TAP_TEMPO_BUTTON:
		playbackHandler.setLedStates();
		break;
	}

	learnedThing = NULL;

	// And, store the actual change
	thingPressedForMidiLearn = newThingPressed;
}

void View::noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int channelOrZone, int note, int velocity) {
	if (thingPressedForMidiLearn) {
		deleteMidiCommandOnRelease = false;

		switch (thingPressedForMidiLearn) {

		case MIDI_LEARN_DRUM_INPUT: {
			// For a Drum, we can assume that the user must be viewing a Clip, as the currentClip.
			((Instrument*)currentSong->currentClip->output)->beenEdited(false);

			// Copy bend ranges if appropriate. This logic is duplicated in NoteRow::setDrum().
			int newBendRange;
			int zone = channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;
			if (zone >= 0) { // MPE input
				newBendRange = fromDevice->mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];
			}
			else { // Regular MIDI input
				newBendRange = fromDevice->inputChannels[channelOrZone].bendRange;
			}

			if (newBendRange) {
				NoteRow* noteRow =
				    ((InstrumentClip*)currentSong->currentClip)->getNoteRowForDrum(drumPressedForMIDILearn);
				if (noteRow) {
					ExpressionParamSet* expressionParams = noteRow->paramManager.getOrCreateExpressionParamSet(true);
					if (expressionParams) {
						if (!expressionParams->params[0].isAutomated()) {
							expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = newBendRange;
						}
					}
				}
			}

			if (drumPressedForMIDILearn->type == DRUM_TYPE_SOUND) {
				currentSong->grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum(
				    fromDevice, (SoundDrum*)drumPressedForMIDILearn, kitPressedForMIDILearn);
			}

			goto recordDetailsOfLearnedThing;
		}

		case MIDI_LEARN_PLAY_BUTTON:
		case MIDI_LEARN_RECORD_BUTTON:
		case MIDI_LEARN_TAP_TEMPO_BUTTON:
			shouldSaveSettingsAfterMidiLearn = true;
			// No break

		default:
recordDetailsOfLearnedThing:
			learnedThing->device = fromDevice;
			learnedThing->channelOrZone = channelOrZone;
			learnedThing->noteOrCC = note;
			break;

		case MIDI_LEARN_MELODIC_INSTRUMENT_INPUT:

			uint8_t newBendRanges[2];

			ParamManager* paramManager = melodicInstrumentPressedForMIDILearn->getParamManager(
			    currentSong); // Could be NULL, e.g. for CVInstruments with no Clips

			// If we already know this incoming MIDI is on an MPE zone...
			if (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE || channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
isMPEZone:
				// Now that we've just learned a MIDI input, update bend ranges from the input device, if they were set, and no automation in activeClip.
				// Same logic can be found in InstrumentClip::changeInstrument().
				int zone = channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;

				newBendRanges[BEND_RANGE_MAIN] = fromDevice->mpeZoneBendRanges[zone][BEND_RANGE_MAIN];
				newBendRanges[BEND_RANGE_FINGER_LEVEL] = fromDevice->mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];

				if (newBendRanges[BEND_RANGE_FINGER_LEVEL]) {
					InstrumentClip* clip = (InstrumentClip*)melodicInstrumentPressedForMIDILearn->activeClip;
					if (!clip || !clip->hasAnyPitchExpressionAutomationOnNoteRows()) {
						if (paramManager) { // Could be NULL, e.g. for CVInstruments with no Clips
							ExpressionParamSet* expressionParams = paramManager->getOrCreateExpressionParamSet();
							if (expressionParams) {
								expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] =
								    newBendRanges[BEND_RANGE_FINGER_LEVEL];
							}
						}
					}
				}
			}

			// Or if we don't already know this is an MPE zone...
			else {
				if (learnedThing->device == fromDevice) {

					if (channelOrZone > highestMIDIChannelSeenWhileLearning) {
						highestMIDIChannelSeenWhileLearning = channelOrZone;
					}
					if (channelOrZone < lowestMIDIChannelSeenWhileLearning) {
						lowestMIDIChannelSeenWhileLearning = channelOrZone;
					}

					// If multiple channels seen, that's a shortcut for setting up MPE zones for the device in question
					if (highestMIDIChannelSeenWhileLearning != lowestMIDIChannelSeenWhileLearning) {
						if (lowestMIDIChannelSeenWhileLearning == 1) {
							fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel =
							    highestMIDIChannelSeenWhileLearning;
							fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].moveUpperZoneOutOfWayOfLowerZone();
							channelOrZone = MIDI_CHANNEL_MPE_LOWER_ZONE;
							MIDIDeviceManager::anyChangesToSave = true;
							goto isMPEZone;
						}
						else if (highestMIDIChannelSeenWhileLearning == 14) {
							fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeUpperZoneLastMemberChannel =
							    lowestMIDIChannelSeenWhileLearning;
							fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].moveLowerZoneOutOfWayOfUpperZone();
							channelOrZone = MIDI_CHANNEL_MPE_UPPER_ZONE;
							MIDIDeviceManager::anyChangesToSave = true;
							goto isMPEZone;
						}
					}
				}

				else { // Or if different device...
					   // Reset our assumptions about MPE
					highestMIDIChannelSeenWhileLearning = channelOrZone;
					lowestMIDIChannelSeenWhileLearning = channelOrZone;
				}

				// If still here, it's not MPE. Now that we know that, see if we want to apply a stored bend range for the input MIDI channel of the device
				newBendRanges[BEND_RANGE_MAIN] = fromDevice->inputChannels[channelOrZone].bendRange;
			}

			if (newBendRanges[BEND_RANGE_MAIN]) {
				if (paramManager) { // Could be NULL, e.g. for CVInstruments with no Clips
					ExpressionParamSet* expressionParams = paramManager->getOrCreateExpressionParamSet();
					if (expressionParams && !expressionParams->params[0].isAutomated()) {
						expressionParams->bendRanges[BEND_RANGE_MAIN] = newBendRanges[BEND_RANGE_MAIN];
					}
				}
			}
			// In a perfect world, we'd also update CVInstrument::cachedBendRanges[]. But that'd only make a difference if it had no Clips.

			// We need to reset the expression params, in case they've gotten stuck. This was mostly prone to happening when doing the "learn MPE input" multi-finger trick.
			clearMelodicInstrumentMonoExpressionIfPossible();

			learnedThing->channelOrZone = channelOrZone;
			learnedThing->device = fromDevice;
			melodicInstrumentPressedForMIDILearn->beenEdited(false); // Why again?

			if (melodicInstrumentPressedForMIDILearn->type == INSTRUMENT_TYPE_SYNTH) {
				currentSong->grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForInstrument(
				    fromDevice, (SoundInstrument*)melodicInstrumentPressedForMIDILearn);
			}

			break;
		}
	}
}

void View::clearMelodicInstrumentMonoExpressionIfPossible() {

	ParamManager* paramManager = melodicInstrumentPressedForMIDILearn->getParamManager(
	    currentSong); // Could be NULL, e.g. for CVInstruments with no Clips

	if (paramManager) {
		ParamCollectionSummary* expressionParamsSummary = paramManager->getExpressionParamSetSummary();
		ExpressionParamSet* expressionParams = (ExpressionParamSet*)expressionParamsSummary->paramCollection;
		if (expressionParams) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithParamCollection* modelStack =
			    setupModelStackWithSong(modelStackMemory, currentSong)
			        ->addTimelineCounter(melodicInstrumentPressedForMIDILearn->activeClip) // Could be NULL
			        ->addOtherTwoThingsButNoNoteRow(melodicInstrumentPressedForMIDILearn->toModControllable(),
			                                        paramManager)
			        ->addParamCollection(expressionParams, expressionParamsSummary);

			expressionParams->clearValues(modelStack);
		}
	}
}

void View::ccReceivedForMIDILearn(MIDIDevice* fromDevice, int channel, int cc, int value) {
	if (thingPressedForMidiLearn) {
		deleteMidiCommandOnRelease = false;

		// For MelodicInstruments...
		if (thingPressedForMidiLearn == MIDI_LEARN_MELODIC_INSTRUMENT_INPUT) {

			// Special case for MIDIInstruments - CCs can learn the input MIDI channel
			if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT) {

				// But only if user hasn't already started learning MPE stuff... Or regular note-ons...
				if (highestMIDIChannelSeenWhileLearning < lowestMIDIChannelSeenWhileLearning) {
					learnedThing->device = fromDevice;
					learnedThing->channelOrZone = channel;
					((Instrument*)currentSong->currentClip->output)->beenEdited(false);
				}
			}
		}

		// Or, for all other types of things the user might be holding down...
		else {

			// So long as the value wasn't 0, pretend it was a note-on for command-learn purposes
			if (value) {
				noteOnReceivedForMidiLearn(fromDevice, channel + IS_A_CC, cc, 127);
			}
		}
	}
}

void View::midiLearnFlash() {
	midiLearnFlashOn = !midiLearnFlashOn;
	uiTimerManager.setTimer(TIMER_MIDI_LEARN_FLASH, fastFlashTime);

	if (getRootUI()) {
		getRootUI()->midiLearnFlash();
	}

	if (midiEngine.globalMIDICommands[GLOBAL_MIDI_COMMAND_PLAY].containsSomething()
	    || thingPressedForMidiLearn == MIDI_LEARN_PLAY_BUTTON) {
		indicator_leds::setLedState(IndicatorLED::PLAY, midiLearnFlashOn);
	}
	if (midiEngine.globalMIDICommands[GLOBAL_MIDI_COMMAND_RECORD].containsSomething()
	    || thingPressedForMidiLearn == MIDI_LEARN_RECORD_BUTTON) {
		indicator_leds::setLedState(IndicatorLED::RECORD, midiLearnFlashOn);
	}
	if (midiEngine.globalMIDICommands[GLOBAL_MIDI_COMMAND_TAP].containsSomething()
	    || thingPressedForMidiLearn == MIDI_LEARN_TAP_TEMPO_BUTTON) {
		indicator_leds::setLedState(IndicatorLED::TAP_TEMPO, midiLearnFlashOn);
	}
}

void View::modEncoderAction(int whichModEncoder, int offset) {

	if (DELUGE_MODEL != DELUGE_MODEL_40_PAD && Buttons::isShiftButtonPressed()) {
		return;
	}

	if (activeModControllableModelStack.modControllable) {

		/*
		char newModelStackMemory[MODEL_STACK_MAX_SIZE];
		copyModelStack(newModelStackMemory, &activeModControllableModelStack, sizeof(ModelStackWithThreeMainThings));
		ModelStackWithThreeMainThings* localModelStack = (ModelStackWithThreeMainThings*)newModelStackMemory;
    	*/

		bool noteTailsAllowedBefore;

		if (activeModControllableModelStack.timelineCounterIsSet()) {

			char modelStackTempMemory[MODEL_STACK_MAX_SIZE];
			copyModelStack(modelStackTempMemory, &activeModControllableModelStack,
			               sizeof(ModelStackWithThreeMainThings));
			ModelStackWithThreeMainThings* tempModelStack = (ModelStackWithThreeMainThings*)modelStackTempMemory;

			noteTailsAllowedBefore =
			    activeModControllableModelStack.modControllable->allowNoteTails(tempModelStack->addSoundFlags());

			bool timelineCounterChanged =
			    activeModControllableModelStack.getTimelineCounter()->possiblyCloneForArrangementRecording(
			        (ModelStackWithTimelineCounter*)&activeModControllableModelStack);

			if (timelineCounterChanged) {

				// We need to get back the NoteRow, ParamManager, and whatever else.
				activeModControllableModelStack.getTimelineCounter()->getActiveModControllable(
				    (ModelStackWithTimelineCounter*)&activeModControllableModelStack);
			}
		}

		{
			ModelStackWithAutoParam* modelStackWithParam =
			    activeModControllableModelStack.modControllable->getParamFromModEncoder(
			        whichModEncoder, &activeModControllableModelStack);

			// If non-existent param, still let the ModControllable know
			if (!modelStackWithParam || !modelStackWithParam->autoParam) {
				int result = activeModControllableModelStack.modControllable->modEncoderActionForNonExistentParam(
				    offset, whichModEncoder, modelStackWithParam);

				if (result == ACTION_RESULT_ACTIONED_AND_CAUSED_CHANGE) {
					setKnobIndicatorLevel(whichModEncoder);
				}
			}

			// Or, if normal case - an actual param
			else {

				char newModelStackMemory[MODEL_STACK_MAX_SIZE];

				// Hack to make it so stutter can't be automated
				if (modelStackWithParam->timelineCounterIsSet()
				    && !modelStackWithParam->paramCollection->doesParamIdAllowAutomation(modelStackWithParam)) {
					copyModelStack(newModelStackMemory, modelStackWithParam, sizeof(ModelStackWithAutoParam));
					modelStackWithParam = (ModelStackWithAutoParam*)newModelStackMemory;
					modelStackWithParam->setTimelineCounter(NULL);
				}

				int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				int knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
				int lowerLimit = getMin(-64, knobPos);
				int newKnobPos = knobPos + offset;
				newKnobPos = getMax(newKnobPos, lowerLimit);
				newKnobPos = getMin(newKnobPos, 64);
				if (newKnobPos == knobPos) {
					return;
				}

				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Perform the actual change
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);

				if (activeModControllableModelStack.timelineCounterIsSet()) {
					char modelStackTempMemory[MODEL_STACK_MAX_SIZE];
					copyModelStack(modelStackTempMemory, modelStackWithParam, sizeof(ModelStackWithThreeMainThings));
					ModelStackWithThreeMainThings* tempModelStack =
					    (ModelStackWithThreeMainThings*)modelStackTempMemory;

					bool noteTailsAllowedAfter =
					    modelStackWithParam->modControllable->allowNoteTails(tempModelStack->addSoundFlags());

					if (noteTailsAllowedBefore != noteTailsAllowedAfter) {
						if (getRootUI() && getRootUI()->isTimelineView()) {
							uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
						}
					}
				}

				if (!newKnobPos
				    && modelStackWithParam->paramCollection->shouldParamIndicateMiddleValue(modelStackWithParam)) {
					indicator_leds::blinkKnobIndicator(whichModEncoder);

					// Make it harder to turn that knob away from its centred position
					Encoders::timeModEncoderLastTurned[whichModEncoder] = AudioEngine::audioSampleTimer - 44100;
				}
				else {
					indicator_leds::stopBlinkingKnobIndicator(whichModEncoder);
				}
			}
		}

		instrumentBeenEdited();
	}
}

void View::instrumentBeenEdited() {
	if (activeModControllableModelStack.timelineCounterIsSet()) {
		activeModControllableModelStack.getTimelineCounter()->instrumentBeenEdited();
	}
}

void View::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// If the learn button is pressed, user is trying to copy or paste, and the fact that we've ended up here means they can't
	if (Buttons::isButtonPressed(hid::button::LEARN)) {
#if !HAVE_OLED
		if (on) {
			numericDriver.displayPopup("CANT");
		}
#endif
		return;
	}

	if (activeModControllableModelStack.modControllable) {

		if (Buttons::isShiftButtonPressed() && on) {

			ModelStackWithAutoParam* modelStackWithParam =
			    activeModControllableModelStack.modControllable->getParamFromModEncoder(
			        whichModEncoder, &activeModControllableModelStack);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				Action* action = actionLogger.getNewAction(ACTION_AUTOMATION_DELETE, false);
				modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);
				numericDriver.displayPopup(HAVE_OLED ? "Automation deleted" : "DELETED");
			}

			return;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		copyModelStack(modelStackMemory, &activeModControllableModelStack, sizeof(ModelStackWithThreeMainThings));
		ModelStackWithThreeMainThings* modelStack = (ModelStackWithThreeMainThings*)modelStackMemory;

		bool anyEditingDone =
		    activeModControllableModelStack.modControllable->modEncoderButtonAction(whichModEncoder, on, modelStack);
		if (anyEditingDone) {
			instrumentBeenEdited();
		}
		setKnobIndicatorLevels(); // These might have changed as a result
		if (getCurrentUI() == &soundEditor) {
			soundEditor.getCurrentMenuItem()->readValueAgain();
		}
	}
}

void View::setKnobIndicatorLevels() {
	if (!getRootUI()) {
		return; // What's this?
	}

	if (activeModControllableModelStack.modControllable) {
		for (int whichModEncoder = 0; whichModEncoder < NUM_LEVEL_INDICATORS; whichModEncoder++) {
			if (!indicator_leds::isKnobIndicatorBlinking(whichModEncoder)) {
				setKnobIndicatorLevel(whichModEncoder);
			}
		}
	}

	else {
		indicator_leds::clearKnobIndicatorLevels();
	}
}

void View::setKnobIndicatorLevel(uint8_t whichModEncoder) {

	// timelineCounter and paramManager could be NULL - if the user is holding down an audition pad in Arranger,
	// and that Output has no Clips. Especially if it's a MIDIInstrument (no ParamManager).
	ModelStackWithAutoParam* modelStackWithParam =
	    activeModControllableModelStack.modControllable->getParamFromModEncoder(
	        whichModEncoder, &activeModControllableModelStack, false);

	int knobPos;

	if (modelStackWithParam->autoParam) {
		int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
		knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
		if (knobPos < -64) {
			knobPos = -64;
		}
		else if (knobPos > 64) {
			knobPos = 64;
		}
	}
	else {
		knobPos =
		    modelStackWithParam->modControllable->getKnobPosForNonExistentParam(whichModEncoder, modelStackWithParam);
	}

	indicator_leds::setKnobIndicatorLevel(whichModEncoder, knobPos + 64);
}

static const uint32_t modButtonUIModes[] = {UI_MODE_AUDITIONING,
                                            UI_MODE_CLIP_PRESSED_IN_SONG_VIEW,
                                            UI_MODE_NOTES_PRESSED,
                                            UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
                                            UI_MODE_HOLDING_ARRANGEMENT_ROW,
                                            UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES,
                                            UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED,
                                            UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED,
                                            0};

void View::modButtonAction(uint8_t whichButton, bool on) {
	pretendModKnobsUntouchedForAWhile();

	if (activeModControllableModelStack.modControllable) {
		if (on) {

			if (isUIModeWithinRange(modButtonUIModes)) {
				activeModControllableModelStack.modControllable->modButtonAction(
				    whichButton, true, (ParamManagerForTimeline*)activeModControllableModelStack.paramManager);

				*activeModControllableModelStack.modControllable->getModKnobMode() = whichButton;

				setKnobIndicatorLevels();
				setModLedStates();
			}
		}
		else {
			activeModControllableModelStack.modControllable->modButtonAction(
			    whichButton, false, (ParamManagerForTimeline*)activeModControllableModelStack.paramManager);
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
			setKnobIndicatorLevels();
#endif
		}
	}
}

void View::setModLedStates() {

	bool itsTheSong = activeModControllableModelStack.getTimelineCounterAllowNull() == currentSong
	                  || (!activeModControllableModelStack.timelineCounterIsSet()
	                      && (getRootUI() == &sessionView || getRootUI() == &arrangerView));

	bool itsAClip = activeModControllableModelStack.timelineCounterIsSet()
	                && activeModControllableModelStack.getTimelineCounter() != currentSong;

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	bool affectEntire = getRootUI() && getRootUI()->getAffectEntire();
	if (!itsTheSong) {
		if (getRootUI() != &instrumentClipView && getRootUI() != &keyboardScreen) {
			affectEntire = true;
		}
		else {
			affectEntire = ((InstrumentClip*)currentSong->currentClip)->affectEntire;
		}
	}
	indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, affectEntire);

	indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, !itsTheSong);
#else
	if (!itsTheSong) {
		bool shouldBlink = false;
		if (getRootUI() == &instrumentClipView) {
			InstrumentClip* clip = (InstrumentClip*)activeModControllableTimelineCounter;
			shouldBlink = (clip->output->type == INSTRUMENT_TYPE_KIT) ? clip->affectEntire : clip->onKeyboardScreen;
		}

		if (!shouldBlink)
			goto noBlinking;
		indicator_leds::blinkLed(IndicatorLED::CLIP_VIEW);
	}

	else {
noBlinking:
		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, !itsTheSong);
	}
#endif

	// Sort out the session/arranger view LEDs
	if (itsTheSong) {
		if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
			indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
		}
		else if (getRootUI() == &arrangerView) {
			indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW);
		}
		else {
			indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, true);
		}
	}
	else {
		indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
	}

	// Sort out actual "mod" LEDs
	int modKnobMode = -1;
	if (activeModControllableModelStack.modControllable) {
		uint8_t* modKnobModePointer = activeModControllableModelStack.modControllable->getModKnobMode();
		if (modKnobModePointer) {
			modKnobMode = *modKnobModePointer;
		}
	}

	for (int i = 0; i < NUM_MOD_BUTTONS; i++) {
		bool on = (i == modKnobMode);
		indicator_leds::setLedState(indicator_leds::modLed[i], on);
	}
}

void View::notifyParamAutomationOccurred(ParamManager* paramManager, bool updateModLevels) {
	if (paramManager == activeModControllableModelStack.paramManager
	    || (getCurrentUI() == &soundEditor && paramManager == soundEditor.currentParamManager)) {

		// If timer wasn't set yet, set it now
		if (!uiTimerManager.isTimerSet(TIMER_DISPLAY_AUTOMATION)) {
			pendingParamAutomationUpdatesModLevels = updateModLevels;
			uiTimerManager.setTimer(TIMER_DISPLAY_AUTOMATION, 25);
		}

		else {
			if (updateModLevels) {
				pendingParamAutomationUpdatesModLevels = true;
			}
		}
	}
}

void View::displayAutomation() {
	if (pendingParamAutomationUpdatesModLevels) {
		setKnobIndicatorLevels();
	}
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->readValueAgain();
	}
}

void View::setActiveModControllableTimelineCounter(TimelineCounter* timelineCounter) {
	if (timelineCounter) {
		timelineCounter = timelineCounter->getTimelineCounterToRecordTo();
	}
	pretendModKnobsUntouchedForAWhile();

	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(&activeModControllableModelStack, currentSong)->addTimelineCounter(timelineCounter);

	if (timelineCounter) {
		timelineCounter->getActiveModControllable(modelStack);
	}

	else {
		modelStack->addOtherTwoThingsButNoNoteRow(NULL, NULL);
	}

	setModLedStates();
	setKnobIndicatorLevels();
}

void View::setActiveModControllableWithoutTimelineCounter(ModControllable* modControllable,
                                                          ParamManager* paramManager) {

	pretendModKnobsUntouchedForAWhile(); // Why again?

	setupModelStackWithSong(&activeModControllableModelStack, currentSong)
	    ->addTimelineCounter(NULL)
	    ->addOtherTwoThingsButNoNoteRow(modControllable, paramManager);

	setModLedStates();
	setKnobIndicatorLevels();
}

void View::setModRegion(uint32_t pos, uint32_t length, int noteRowId) {

	modPos = pos;
	modLength = length;
	modNoteRowId = noteRowId;

	pretendModKnobsUntouchedForAWhile();

	// If holding down a note and not playing, permanently grab values from pos
	if (length && activeModControllableModelStack.timelineCounterIsSet()
	    && activeModControllableModelStack.modControllable && activeModControllableModelStack.paramManager
	    && !playbackHandler.isEitherClockActive()
	    && activeModControllableModelStack.paramManager->containsAnyMainParamCollections()) {

		activeModControllableModelStack.paramManager->toForTimeline()->grabValuesFromPos(
		    pos, &activeModControllableModelStack);
		// activeModControllable might not be a Sound, but in that case, the pointer's not going to get used
	}
	setKnobIndicatorLevels();
}

void View::pretendModKnobsUntouchedForAWhile() {
	Encoders::timeModEncoderLastTurned[0] = Encoders::timeModEncoderLastTurned[1] =
	    AudioEngine::audioSampleTimer - 44100;
}

void View::cycleThroughReverbPresets() {

	int currentRoomSize = AudioEngine::reverb.getroomsize() * 50;
	int currentDampening = AudioEngine::reverb.getdamp() * 50;

	// See which preset we're the closest to currently
	int lowestDifferentness = 1000;
	int currentPreset;
	for (int p = 0; p < NUM_PRESET_REVERBS; p++) {
		int differentness =
		    std::abs(currentRoomSize - presetReverbRoomSize[p]) + std::abs(currentDampening - presetReverbDampening[p]);
		if (differentness < lowestDifferentness) {
			lowestDifferentness = differentness;
			currentPreset = p;
		}
	}

	int newPreset = currentPreset + 1;
	if (newPreset >= NUM_PRESET_REVERBS) {
		newPreset = 0;
	}

	AudioEngine::reverb.setroomsize((float)presetReverbRoomSize[newPreset] / 50);
	AudioEngine::reverb.setdamp((float)presetReverbDampening[newPreset] / 50);

	numericDriver.displayPopup(presetReverbNames[newPreset]);
}

// If HAVE_OLED, must make sure OLED::sendMainImage() gets called after this.
void View::displayOutputName(Output* output, bool doBlink, Clip* clip) {

	int channel, channelSuffix;
	bool editedByUser = true;
	if (output->type != OUTPUT_TYPE_AUDIO) {
		Instrument* instrument = (Instrument*)output;
		editedByUser = !instrument->existsOnCard;
		switch (output->type) {
		case INSTRUMENT_TYPE_MIDI_OUT:
			channelSuffix = ((MIDIInstrument*)instrument)->channelSuffix;
			// No break

		case INSTRUMENT_TYPE_CV:
			channel = ((NonAudioInstrument*)instrument)->channel;
			break;
		}
	}

	drawOutputNameFromDetails(output->type, channel, channelSuffix, output->name.get(), editedByUser, doBlink, clip);
}

// If HAVE_OLED, must make sure OLED::sendMainImage() gets called after this.
void View::drawOutputNameFromDetails(int outputType, int channel, int channelSuffix, char const* name,
                                     bool editedByUser, bool doBlink, Clip* clip) {
	if (doBlink) {
		using namespace indicator_leds;
		LED led;

		if (outputType == INSTRUMENT_TYPE_SYNTH) {
			led = LED::SYNTH;
		}
		else {
			setLedState(LED::SYNTH, false);
		}

		if (outputType == INSTRUMENT_TYPE_KIT) {
			led = LED::KIT;
		}
		else {
			setLedState(LED::KIT, false);
		}

		if (outputType == INSTRUMENT_TYPE_MIDI_OUT) {
			led = LED::MIDI;
		}
		else {
			setLedState(LED::MIDI, false);
		}

		if (outputType == INSTRUMENT_TYPE_CV) {
			led = LED::CV;
		}
		else {
			setLedState(LED::CV, false);
		}

		if (outputType != OUTPUT_TYPE_AUDIO) {
			blinkLed(led);
		}

		InstrumentClip* clip = NULL;
		if (clip && clip->type == CLIP_TYPE_INSTRUMENT) {
			clip = (InstrumentClip*)clip;
		}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
		setLedState(LED::KEYBOARD, (clip && clip->onKeyboardScreen));
#endif
		setLedState(LED::SCALE_MODE, (clip && clip->inScaleMode && clip->output->type != INSTRUMENT_TYPE_KIT));
		setLedState(LED::CROSS_SCREEN_EDIT, (clip && clip->wrapEditing));
	}

#if HAVE_OLED
	OLED::clearMainImage();
	char const* outputTypeText;
	switch (outputType) {
	case INSTRUMENT_TYPE_SYNTH:
		outputTypeText = "Synth";
		break;
	case INSTRUMENT_TYPE_KIT:
		outputTypeText = "Kit";
		break;
	case INSTRUMENT_TYPE_MIDI_OUT:
		outputTypeText = (channel < 16) ? "MIDI channel" : "MPE zone";
		break;
	case INSTRUMENT_TYPE_CV:
		outputTypeText = "CV / gate channel";
		break;
	case OUTPUT_TYPE_AUDIO:
		outputTypeText = "Audio track";
		break;
	default:
		__builtin_unreachable();
	}

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	OLED::drawStringCentred(outputTypeText, yPos, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                        TEXT_SPACING_Y);
	char buffer[12];
	char const* nameToDraw;
#endif

	if (name && name[0]) {
#if HAVE_OLED
		nameToDraw = name;
oledDrawString:
#if OLED_MAIN_HEIGHT_PIXELS == 64
		int yPos = OLED_MAIN_TOPMOST_PIXEL + 32;
#else
		int yPos = OLED_MAIN_TOPMOST_PIXEL + 21;
#endif

		int textSpacingX = TEXT_TITLE_SPACING_X;
		int textSpacingY = TEXT_TITLE_SIZE_Y;

		int textLength = strlen(name);
		int stringLengthPixels = textLength * textSpacingX;
		if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
			OLED::drawStringCentred(nameToDraw, yPos, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, textSpacingX,
			                        textSpacingY);
		}
		else {
			OLED::drawString(nameToDraw, 0, yPos, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, textSpacingX,
			                 textSpacingY);
			OLED::setupSideScroller(0, name, 0, OLED_MAIN_WIDTH_PIXELS, yPos, yPos + textSpacingY, textSpacingX,
			                        textSpacingY, false);
		}

#else
		bool andAHalf;
		if (numericDriver.getEncodedPosFromLeft(99999, name, &andAHalf) > NUMERIC_DISPLAY_LENGTH) { // doBlink &&
			numericDriver.setScrollingText(name, 0, initialFlashTime + flashTime);
		}
		else {
			// If numeric-looking, we might want to align right.
			bool alignRight = false;
			uint8_t dotPos = 255;

			char const* charPos = name;
			if (*charPos == '0') { // If first digit is 0, then no more digits allowed.
				charPos++;
			}
			else { // Otherwise, up to 3 digits allowed.
				while (*charPos >= '0' && *charPos <= '9' && charPos < (name + 3)) {
					charPos++;
				}
			}

			if (charPos != name) { // We are required to have found at least 1 digit.
				if (*charPos == 0) {
yesAlignRight:
					alignRight = true;
					if (!editedByUser) {
						dotPos = 3;
					}
				}
				else if ((*charPos >= 'a' && *charPos <= 'z') || (*charPos >= 'A' && *charPos <= 'Z')) {
					charPos++;
					if (*charPos == 0) {
						goto yesAlignRight;
					}
				}
			}

			numericDriver.setText(name, alignRight, dotPos, doBlink);
		}
#endif
	}
	else if (outputType == INSTRUMENT_TYPE_MIDI_OUT) {
#if HAVE_OLED
		if (channel < 16) {
			slotToString(channel + 1, channelSuffix, buffer, 1);
			goto oledOutputBuffer;
		}
		else {
			nameToDraw = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
			goto oledDrawString;
		}
#else
		if (channel < 16) {
			numericDriver.setTextAsSlot(channel + 1, channelSuffix, false, doBlink);
		}
		else {
			char const* text = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
			numericDriver.setText(text, false, 255, doBlink);
		}
#endif
	}
	else if (outputType == INSTRUMENT_TYPE_CV) {
#if HAVE_OLED
		intToString(channel + 1, buffer);
oledOutputBuffer:
		nameToDraw = buffer;
		goto oledDrawString;
#else
		numericDriver.setTextAsNumber(channel + 1, 255, doBlink);
#endif
	}
}

void View::navigateThroughAudioOutputsForAudioClip(int offset, AudioClip* clip, bool doBlink) {

	AudioEngine::logAction("navigateThroughPresets");

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	// Work out availabilityRequirement. But we don't in this case need to think about whether the Output can be "replaced" - that's for InstrumentClips
	int availabilityRequirement;
	currentSong->canOldOutputBeReplaced(clip, &availabilityRequirement);

	if (availabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
		numericDriver.displayPopup(HAVE_OLED ? "Clip has instances in arranger" : "CANT");
		return;
	}

	Output* newOutput = currentSong->getNextAudioOutput(offset, clip->output, availabilityRequirement);

	if (newOutput != clip->output) {

		Output* oldOutput = clip->output;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		clip->changeOutput(modelStack->addTimelineCounter(clip), newOutput);

		oldOutput->pickAnActiveClipIfPossible(modelStack, true);
	}

	displayOutputName(newOutput, doBlink);
#if HAVE_OLED
	OLED::sendMainImage();
#endif

	setActiveModControllableTimelineCounter(clip); // Necessary? Does ParamManager get moved over too?
}

void View::navigateThroughPresetsForInstrumentClip(int offset, ModelStackWithTimelineCounter* modelStack,
                                                   bool doBlink) {

	AudioEngine::logAction("navigateThroughPresets");

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	int oldSubMode = currentUIMode; // We may have been holding down a clip in Session View

	actionLogger.deleteAllLogs(); // Can't undo past this!

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	uint8_t instrumentType = clip->output->type;

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E057", "H057");

	// Work out availabilityRequirement. This can't change as presets are navigated through... I don't think?
	int availabilityRequirement;
	bool oldInstrumentCanBeReplaced = modelStack->song->canOldOutputBeReplaced(clip, &availabilityRequirement);

	bool shouldReplaceWholeInstrument;

	Instrument* newInstrument;
	Instrument* oldInstrument = (Instrument*)clip->output;

	// If we're in MIDI or CV mode, easy - just change the channel
	if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT || instrumentType == INSTRUMENT_TYPE_CV) {

		NonAudioInstrument* oldNonAudioInstrument = (NonAudioInstrument*)oldInstrument;
		int newChannel = oldNonAudioInstrument->channel;
		int newChannelSuffix;
		if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
			newChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
		}

		// TODO: the contents of these badly wants to be replaced with how I did it in changeInstrumentType()!

		// CV
		if (instrumentType == INSTRUMENT_TYPE_CV) {
			while (true) {
				newChannel = (newChannel + offset) & (NUM_CV_CHANNELS - 1);

				if (newChannel == oldNonAudioInstrument->channel) {
					numericDriver.displayPopup(HAVE_OLED ? "No unused channels" : "CANT");
					return;
				}

				if (availabilityRequirement == AVAILABILITY_ANY) {
					break;
				}
				else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION) {
					if (!modelStack->song->doesNonAudioSlotHaveActiveClipInSession(instrumentType, newChannel)) {
						break;
					}
				}
				else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
					if (!modelStack->song->getInstrumentFromPresetSlot(instrumentType, newChannel, -1, NULL, NULL,
					                                                   false)) {
						break;
					}
				}
			}
		}

		// Or MIDI
		else {

			int oldChannel = newChannel;

			if (oldInstrumentCanBeReplaced) {
				oldNonAudioInstrument->channel = -1; // Get it out of the way
			}

			while (true) {
				newChannelSuffix += offset;

				// Turned left
				if (offset == -1) {
					if (newChannelSuffix < -1) {
						newChannel = (newChannel + offset);
						if (newChannel < 0) {
							newChannel = 17;
						}
						newChannelSuffix = modelStack->song->getMaxMIDIChannelSuffix(newChannel);
					}
				}

				// Turned right
				else {

					if (newChannelSuffix >= 26
					    || newChannelSuffix > modelStack->song->getMaxMIDIChannelSuffix(newChannel)) {
						newChannel = (newChannel + offset);
						if (newChannel >= 18) {
							newChannel = 0;
						}
						newChannelSuffix = -1;
					}
				}

				if (newChannel == oldChannel
				    && newChannelSuffix == ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix) {
					oldNonAudioInstrument->channel = oldChannel; // Put it back
					numericDriver.displayPopup(HAVE_OLED ? "No unused channels" : "CANT");
					return;
				}

				if (availabilityRequirement == AVAILABILITY_ANY) {
					break;
				}
				else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_AVAILABLE_IN_SESSION) {
					if (!modelStack->song->doesNonAudioSlotHaveActiveClipInSession(instrumentType, newChannel,
					                                                               newChannelSuffix)) {
						break;
					}
				}
				else if (availabilityRequirement == AVAILABILITY_INSTRUMENT_UNUSED) {
					if (!modelStack->song->getInstrumentFromPresetSlot(instrumentType, newChannel, newChannelSuffix,
					                                                   NULL, NULL, false)) {
						break;
					}
				}
			}

			oldNonAudioInstrument->channel = oldChannel; // Put it back
		}

		newInstrument = modelStack->song->getInstrumentFromPresetSlot(instrumentType, newChannel, newChannelSuffix,
		                                                              NULL, NULL, false);

		shouldReplaceWholeInstrument = (oldInstrumentCanBeReplaced && !newInstrument);

		// If we want to "replace" the old Instrument, we can instead sneakily just modify its channel
		if (shouldReplaceWholeInstrument) {
			if (playbackHandler.isEitherClockActive()) {
				clip->output->activeClip->expectNoFurtherTicks(modelStack->song);
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			memcpy(modelStackMemory, modelStack, sizeof(ModelStack));

			clip->output->stopAnyAuditioning((ModelStack*)modelStackMemory);

			// Because these are just MIDI / CV instruments and we're changing them for all Clips, we can just change the existing Instrument object!
			oldNonAudioInstrument->channel = newChannel;
			if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
				((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix = newChannelSuffix;
			}

			newInstrument = oldNonAudioInstrument;
		}

		// Otherwise...
		else {

			bool instrumentAlreadyInSong = (newInstrument != NULL);

			// If an Instrument doesn't yet exist for the new channel we're gonna use...
			if (!newInstrument) {
				if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
					newInstrument = modelStack->song->grabHibernatingMIDIInstrument(newChannel, newChannelSuffix);
					if (newInstrument) {
						goto gotAnInstrument;
					}
				}
				newInstrument =
				    storageManager.createNewNonAudioInstrument(instrumentType, newChannel, newChannelSuffix);
				if (!newInstrument) {
					numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
					return;
				}

				// We just allocated a brand new Instrument in RAM. If MIDI, copy knob assignments from old Instrument
				if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
					MIDIInstrument* newMIDIInstrument = (MIDIInstrument*)newInstrument;
					MIDIInstrument* oldMIDIInstrument = (MIDIInstrument*)clip->output;
					memcpy(newMIDIInstrument->modKnobCCAssignments, oldMIDIInstrument->modKnobCCAssignments,
					       sizeof(oldMIDIInstrument->modKnobCCAssignments));
					newInstrument->editedByUser =
					    oldNonAudioInstrument
					        ->editedByUser; // This keeps a record of "whether there are any CC assignments", so must be copied across
				}

				// And, we'd better copy the default velocity too
				newInstrument->defaultVelocity = oldNonAudioInstrument->defaultVelocity;
			}
gotAnInstrument:

			int error = clip->changeInstrument(modelStack, newInstrument, NULL,
			                                   INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED, NULL, true);
			// TODO: deal with errors

			if (!instrumentAlreadyInSong) {
				modelStack->song->addOutput(newInstrument);
			}
		}

		displayOutputName(newInstrument, doBlink);
#if HAVE_OLED
		OLED::sendMainImage();
#endif
	}

	// Or if we're on a Kit or Synth...
	else {

		PresetNavigationResult results =
		    loadInstrumentPresetUI.doPresetNavigation(offset, oldInstrument, availabilityRequirement, false);
		if (results.error == NO_ERROR_BUT_GET_OUT) {
getOut:
#if HAVE_OLED
			OLED::removeWorkingAnimation();
#endif
			return;
		}
		else if (results.error) {
			numericDriver.displayError(results.error);
			goto getOut;
		}

		newInstrument = results.fileItem->instrument;
		bool instrumentAlreadyInSong = results.fileItem->instrumentAlreadyInSong;
		Browser::emptyFileItems();

		// For Kits, ensure that every SoundDrum has a ParamManager somewhere
#if ALPHA_OR_BETA_VERSION
		if (newInstrument->type == INSTRUMENT_TYPE_KIT) {
			Kit* kit = (Kit*)newInstrument;
			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					if (!modelStack->song->getBackedUpParamManagerPreferablyWithClip(
					        soundDrum, NULL)) { // If no backedUpParamManager...
						if (!modelStack->song->findParamManagerForDrum(
						        kit, soundDrum)) { // If no ParamManager with a NoteRow somewhere...

							if (results.loadedFromFile) {
								numericDriver.freezeWithError("E103");
							}
							else if (instrumentAlreadyInSong) {
								numericDriver.freezeWithError("E104");
							}
							else {
								numericDriver.freezeWithError(
								    "E105"); // Sven got - very rare! This means Kit was hibernating, I guess.
							}
						}
					}
				}
			}
		}
#endif

		shouldReplaceWholeInstrument = (oldInstrumentCanBeReplaced && !instrumentAlreadyInSong);

		// If swapping whole Instrument...
		if (shouldReplaceWholeInstrument) {

			// We know the Instrument hasn't been added to the Song, and this call will do it
			modelStack->song->replaceInstrument(oldInstrument, newInstrument);
		}

		// Otherwise, just changeInstrument() for this one Clip
		else {

			// If that Instrument wasn't already in use in the Song, copy default velocity over
			newInstrument->defaultVelocity = oldInstrument->defaultVelocity;

			// If we're here, we know the Clip is not playing in the arranger (and doesn't even have an instance in there)

			int error = clip->changeInstrument(modelStack, newInstrument, NULL,
			                                   INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED, NULL, true);
			// TODO: deal with errors!

			if (!instrumentAlreadyInSong) {
				modelStack->song->addOutput(newInstrument);
			}
		}

		// Kit-specific stuff
		if (instrumentType == INSTRUMENT_TYPE_KIT) {
			clip->ensureScrollWithinKitBounds();
			((Kit*)newInstrument)->selectedDrum = NULL;
		}

		if (getCurrentUI() == &instrumentClipView) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			instrumentClipView.recalculateColours();
			uiNeedsRendering(&instrumentClipView);
		}

#if HAVE_OLED
		OLED::removeWorkingAnimation();
#else
		numericDriver.removeTopLayer();
#endif
	}

	instrumentChanged(modelStack, newInstrument);

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager(
	    "E058",
	    "H058"); // I got this during limited-RAM testing. Maybe there wasn't enough RAM to create the ParamManager or store its backup?
}

// Returns whether success
bool View::changeInstrumentType(int newInstrumentType, ModelStackWithTimelineCounter* modelStack, bool doBlink) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int oldInstrumentType = clip->output->type;
	if (oldInstrumentType == newInstrumentType) {
		return false;
	}

	Instrument* newInstrument = clip->changeInstrumentType(modelStack, newInstrumentType);
	if (!newInstrument) {
		return false;
	}

	setActiveModControllableTimelineCounter(clip); // Do a redraw. Obviously the Clip is the same
	displayOutputName(newInstrument, doBlink);
#if HAVE_OLED
	OLED::sendMainImage();
#endif

	return true;
}

void View::instrumentChanged(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument) {

	((Clip*)modelStack->getTimelineCounter())->outputChanged(modelStack, newInstrument);
	setActiveModControllableTimelineCounter(
	    modelStack->getTimelineCounter()); // Do a redraw. Obviously the Clip is the same
}

void View::getClipMuteSquareColour(Clip* clip, uint8_t thisColour[]) {

	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING && clip && clip->armedForRecording) {
		if (blinkOn) {
			bool shouldGoPurple = clip->type == CLIP_TYPE_AUDIO && ((AudioClip*)clip)->overdubsShouldCloneOutput;

			// Bright colour
			if (clip->wantsToBeginLinearRecording(currentSong)) {
				if (shouldGoPurple) {
					thisColour[0] = 128;
					thisColour[1] = 0;
					thisColour[2] = 128;
				}
				else {
					thisColour[0] = 255;
					thisColour[1] = 1;
					thisColour[2] = 0;
				}
			}

			// Dull colour, cos can't actually begin linear recording despite being armed
			else {
				if (shouldGoPurple) {
					thisColour[0] = 60;
					thisColour[1] = 15;
					thisColour[2] = 60;
				}
				else {
					thisColour[0] = 60;
					thisColour[1] = 15;
					thisColour[2] = 15;
				}
			}
		}
		else {
			memset(thisColour, 0, 3);
		}
		return;
	}

	// If user assigning MIDI controls and this Clip has a command assigned, flash pink
	if (midiLearnFlashOn && clip->muteMIDICommand.containsSomething()) {
		thisColour[0] = midiCommandColourRed;
		thisColour[1] = midiCommandColourGreen;
		thisColour[2] = midiCommandColourBlue;
		return;
	}

	if (clipArmFlashOn && clip->armState) {
		thisColour[0] = 0;
		thisColour[1] = 0;
		thisColour[2] = 0;
	}

	// If it's soloed or armed to solo, blue
	else if (clip->soloingInSessionMode || clip->armState == ARM_STATE_ON_TO_SOLO) {
		menu_item::soloColourMenu.getRGB(thisColour);
	}

	// Or if not soloing...
	else {

		// If it's stopped, red.
		if (!clip->activeIfNoSolo) {
			menu_item::stoppedColourMenu.getRGB(thisColour);
		}

		// Or, green.
		else {
			menu_item::activeColourMenu.getRGB(thisColour);
		}

		if (currentSong->getAnyClipsSoloing()) {
			dimColour(thisColour);
		}
	}

	// If user assigning MIDI controls and has this Clip selected, flash to half brightness
	if (midiLearnFlashOn && learnedThing == &clip->muteMIDICommand) {
		thisColour[0] >>= 1;
		thisColour[1] >>= 1;
		thisColour[2] >>= 1;
	}
}

extern int8_t defaultAudioClipOverdubOutputCloning;

int View::clipStatusPadAction(Clip* clip, bool on, int yDisplayIfInSessionView) {

	switch (currentUIMode) {
	case UI_MODE_MIDI_LEARN:
		if (sdRoutineLock) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		view.clipStatusMidiLearnPadPressed(on, clip);
		if (!on) {
			uiNeedsRendering(&sessionView, 0, 1 << yDisplayIfInSessionView);
		}
		break;

	case UI_MODE_VIEWING_RECORD_ARMING:
		if (on) {
			if (!clip->armedForRecording) {
				clip->armedForRecording = true;
				if (clip->type == CLIP_TYPE_AUDIO) {
					((AudioClip*)clip)->overdubsShouldCloneOutput = false;
					defaultAudioClipOverdubOutputCloning = 0;
				}
			}
			else {
				if (clip->type == CLIP_TYPE_AUDIO && !((AudioClip*)clip)->overdubsShouldCloneOutput) {
					((AudioClip*)clip)->overdubsShouldCloneOutput = true;
					defaultAudioClipOverdubOutputCloning = 1;
					break; // No need to reassess greyout
				}
				else {
					clip->armedForRecording = false;
				}
			}
			PadLEDs::reassessGreyout(true);
		}
		break;

	case UI_MODE_NONE:
		// If the user was just quick and is actually holding the record button but the submode just hasn't changed yet...
		if (on && Buttons::isButtonPressed(hid::button::RECORD)) {
			clip->armedForRecording = !clip->armedForRecording;
			sessionView
			    .timerCallback(); // Get into UI_MODE_VIEWING_RECORD_ARMING. TODO: this needs doing properly - what if we're in a Clip view?
			break;
		}
		// No break
	case UI_MODE_CLIP_PRESSED_IN_SONG_VIEW:
	case UI_MODE_STUTTERING:
		if (on) {
			sessionView.performActionOnPadRelease = false; // Even though there's a chance we're not in session view
			session.toggleClipStatus(clip, NULL, Buttons::isShiftButtonPressed(), INTERNAL_BUTTON_PRESS_LATENCY);
		}
		break;

#ifdef soloButtonX
	case UI_MODE_SOLO_BUTTON_HELD:
#else
	case UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON:
#endif
		if (on) {
			sessionView.performActionOnPadRelease = false; // Even though there's a chance we're not in session view
			session.soloClipAction(clip, INTERNAL_BUTTON_PRESS_LATENCY);
		}
		break;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void View::flashPlayEnable() {
	uiTimerManager.setTimer(TIMER_PLAY_ENABLE_FLASH, fastFlashTime);
}

void View::flashPlayDisable() {
	clipArmFlashOn = false;
	uiTimerManager.unsetTimer(TIMER_PLAY_ENABLE_FLASH);

	if (getRootUI() == &sessionView) {
		uiNeedsRendering(&sessionView, 0, 0xFFFFFFFF);
	}
#ifdef currentClipStatusButtonX
	else if (getRootUI()->toClipMinder()) {
		drawCurrentClipPad(currentSong->currentClip);
	}
#endif
}

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = setupModelStack(modelStackMemory);
*/
