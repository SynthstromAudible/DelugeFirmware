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

#include "gui/views/view.h"
#include "definitions_cxx.hpp"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "extern.h"
#include "gui/colour.h"
#include "gui/context_menu/clear_song.h"
#include "gui/context_menu/launch_style.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/print.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/consequence/consequence.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

View view{};

extern GlobalMIDICommand pendingGlobalMIDICommand;

View::View() {
	midiLearnFlashOn = false;

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

extern GlobalMIDICommand pendingGlobalMIDICommandNumClustersWritten;

ActionResult View::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	GlobalMIDICommand newGlobalMidiCommand;

	// Tap tempo button. Shouldn't move this to MatrixDriver, because this code can put us in tapTempo mode, and other UIs aren't built to
	// handle this
	if (b == TAP_TEMPO) {

		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (on) {
				deleteMidiCommandOnRelease = true;
				endMidiLearnPressSession(MidiLearn::TAP_TEMPO_BUTTON);
				learnedThing = &midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TAP)];
			}

			else if (thingPressedForMidiLearn == MidiLearn::TAP_TEMPO_BUTTON) {
doEndMidiLearnPressSession:
				if (deleteMidiCommandOnRelease) {
					learnedThing->clear();
					shouldSaveSettingsAfterMidiLearn = true;
				}
				endMidiLearnPressSession();
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
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN) {
				thingPressedForMidiLearn = MidiLearn::NONE;
				shouldSaveSettingsAfterMidiLearn = false;
				currentUIMode = UI_MODE_MIDI_LEARN;
				midiLearnFlash();
				indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1);
			}
		}
		else {
			endMIDILearn();
		}
	}

	// Play button for MIDI learn
	else if (b == PLAY && currentUIMode == UI_MODE_MIDI_LEARN) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			deleteMidiCommandOnRelease = true;
			endMidiLearnPressSession(MidiLearn::PLAY_BUTTON);
			learnedThing = &midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)];
		}
		else if (thingPressedForMidiLearn == MidiLearn::PLAY_BUTTON) {
			goto doEndMidiLearnPressSession;
		}
	}

	// Record button for MIDI learn
	else if (b == RECORD && currentUIMode == UI_MODE_MIDI_LEARN) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		if (on) {
			deleteMidiCommandOnRelease = true;
			endMidiLearnPressSession(MidiLearn::RECORD_BUTTON);
			learnedThing = &midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::RECORD)];
		}
		else if (thingPressedForMidiLearn == MidiLearn::RECORD_BUTTON) {
			goto doEndMidiLearnPressSession;
		}
	}

	// Save button
	else if (b == SAVE) {

		if (!Buttons::isButtonPressed(deluge::hid::button::SYNTH) && !Buttons::isButtonPressed(deluge::hid::button::KIT)
		    && !Buttons::isButtonPressed(deluge::hid::button::MIDI)
		    && !Buttons::isButtonPressed(deluge::hid::button::CV)
		    && !((getRootUI() == &performanceSessionView) && Buttons::isButtonPressed(deluge::hid::button::KEYBOARD))) {
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
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					currentUIMode = UI_MODE_NONE;

					if ((int32_t)(AudioEngine::audioSampleTimer - timeSaveButtonPressed) < kShortPressTime) {
						if (currentSong->hasAnyPendingNextOverdubs()) {
							display->displayPopup(
							    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_SAVE_WHILE_OVERDUBS_PENDING));
						}
						else {
							openUI(&saveSongUI);
						}
					}
					else {
						indicator_leds::setLedState(IndicatorLED::SAVE, false);
					}
				}
				else if (currentUIMode == UI_MODE_NONE) {
					indicator_leds::setLedState(IndicatorLED::SAVE, false);
				}
			}
		}
	}

	// Load button
	else if (b == LOAD) {

		if (!Buttons::isButtonPressed(deluge::hid::button::SYNTH) && !Buttons::isButtonPressed(deluge::hid::button::KIT)
		    && !Buttons::isButtonPressed(deluge::hid::button::MIDI)
		    && !Buttons::isButtonPressed(deluge::hid::button::CV)
		    && !((getRootUI() == &performanceSessionView) && Buttons::isButtonPressed(deluge::hid::button::KEYBOARD))) {
			// Press down
			if (on) {
				if (currentUIMode == UI_MODE_NONE) {

					if (Buttons::isShiftButtonPressed()) {
						if (inCardRoutine) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}
					currentUIMode = UI_MODE_NONE;

					if ((int32_t)(AudioEngine::audioSampleTimer - timeSaveButtonPressed) < kShortPressTime) {
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
				else if (currentUIMode == UI_MODE_NONE) {
					indicator_leds::setLedState(IndicatorLED::LOAD, false);
				}
			}
		}
	}

	// Sync-scaling button - can be repurposed as Fill Mode in community settings
	else if (b == SYNC_SCALING) {
		if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction)
		     == RuntimeFeatureStateSyncScalingAction::Fill)) {
			currentSong->changeFillMode(on);
		}
		else if (on && currentUIMode == UI_MODE_NONE) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
cant:
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// If no scaling currently, start it, if we're on a Clip-minder screen
			if (!currentSong->getSyncScalingClip()) {
				if (!getCurrentUI()->toClipMinder()) {
					indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW);
					return ActionResult::DEALT_WITH;
				}

				// Can't do it for arranger-only Clips
				if (getCurrentClip()->isArrangementOnlyClip()) {
					goto cant;
				}

				// Can't do it for Clips recording linearly
				if (getCurrentClip()->getCurrentlyRecordingLinearly()) {
					goto cant;
				}

				currentSong->setInputTickScaleClip(getCurrentClip());
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
				    Buttons::isShiftButtonPressed() ? GlobalMIDICommand::REDO : GlobalMIDICommand::UNDO;
				pendingGlobalMIDICommandNumClustersWritten = GlobalMIDICommand::PLAYBACK_RESTART; // Bug hunting.
				playbackHandler.slowRoutine(); // Do it now if not reading card.
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
		newGlobalMidiCommand = GlobalMIDICommand::UNDO;
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
		newGlobalMidiCommand = GlobalMIDICommand::REDO;
		goto possiblyRevert;
	}
#endif

	// Select button with shift - go to settings menu
	else if (b == SELECT_ENC && Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			display->setNextTransitionDirection(1);
			soundEditor.setup();
			openUI(&soundEditor);
		}
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
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

	if (currentUIMode == UI_MODE_MIDI_LEARN) {
		currentUIMode = UI_MODE_NONE;
	}

	playbackHandler.setLedStates();
	indicator_leds::setLedState(IndicatorLED::LEARN, false);
}

void View::setTimeBaseScaleLedState() {
	// If this Clip is the inputTickScaleClip, flash the LED
	if (getCurrentUI()->toClipMinder() && getCurrentClip() == currentSong->getSyncScalingClip()) {
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
		endMidiLearnPressSession(MidiLearn::SECTION);
		deleteMidiCommandOnRelease = true;
		learnedThing = &currentSong->sections[section].launchMIDICommand;
	}
	else if (thingPressedForMidiLearn == MidiLearn::SECTION) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
		}
		endMidiLearnPressSession();
	}
}

void View::clipStatusMidiLearnPadPressed(bool on, Clip* whichClip) {
	if (on) {
		endMidiLearnPressSession(MidiLearn::CLIP);
		deleteMidiCommandOnRelease = true;
		learnedThing = &whichClip->muteMIDICommand;
	}
	else if (thingPressedForMidiLearn == MidiLearn::CLIP) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
		}
		endMidiLearnPressSession();
	}
}

void View::noteRowMuteMidiLearnPadPressed(bool on, NoteRow* whichNoteRow) {
	if (on) {
		endMidiLearnPressSession(MidiLearn::NOTEROW_MUTE);
		deleteMidiCommandOnRelease = true;
		learnedThing = &whichNoteRow->drum->muteMIDICommand;
	}
	else if (thingPressedForMidiLearn == MidiLearn::NOTEROW_MUTE) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
		}
		endMidiLearnPressSession();
	}
}

void View::drumMidiLearnPadPressed(bool on, Drum* drum, Kit* kit) {
	if (on) {
		endMidiLearnPressSession(MidiLearn::DRUM_INPUT);
		deleteMidiCommandOnRelease = true;
		learnedThing = &drum->midiInput;
		drumPressedForMIDILearn = drum;
		kitPressedForMIDILearn =
		    kit; // Having this makes it possible to search much faster when we call grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum()
	}

	else if (thingPressedForMidiLearn == MidiLearn::DRUM_INPUT) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
			getCurrentInstrument()->beenEdited(false);
		}
		endMidiLearnPressSession();
	}
}

void View::melodicInstrumentMidiLearnPadPressed(bool on, MelodicInstrument* instrument) {
	if (on) {
		endMidiLearnPressSession(MidiLearn::MELODIC_INSTRUMENT_INPUT);
		deleteMidiCommandOnRelease = true;
		learnedThing = &instrument->midiInput;
		melodicInstrumentPressedForMIDILearn = instrument;
		highestMIDIChannelSeenWhileLearning = -1;
		lowestMIDIChannelSeenWhileLearning = 16;
	}

	else if (thingPressedForMidiLearn == MidiLearn::MELODIC_INSTRUMENT_INPUT) {
		if (deleteMidiCommandOnRelease) {
			clearMelodicInstrumentMonoExpressionIfPossible(); // In case it gets "stuck".
			learnedThing->clear();
			instrument->beenEdited(false);
		}
		endMidiLearnPressSession();
	}
}

void View::endMidiLearnPressSession(MidiLearn newThingPressed) {
	// Depending on which thing was previously pressed, we might have to do some admin
	switch (thingPressedForMidiLearn) {
	case MidiLearn::PLAY_BUTTON:
	case MidiLearn::RECORD_BUTTON:
	case MidiLearn::TAP_TEMPO_BUTTON:
		playbackHandler.setLedStates();
		break;
	}

	learnedThing = NULL;

	// And, store the actual change
	thingPressedForMidiLearn = newThingPressed;

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook::HOOK_ON_MIDI_LEARN);
}

void View::noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channelOrZone, int32_t note, int32_t velocity) {
	if (thingPressedForMidiLearn != MidiLearn::NONE) {
		deleteMidiCommandOnRelease = false;

		switch (thingPressedForMidiLearn) {

		case MidiLearn::DRUM_INPUT: {
			// For a Drum, we can assume that the user must be viewing a Clip, as the currentClip.
			getCurrentInstrument()->beenEdited(false);

			// Copy bend ranges if appropriate. This logic is duplicated in NoteRow::setDrum().
			int32_t newBendRange;
			int32_t zone = channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;
			if (zone >= 0) { // MPE input
				newBendRange = fromDevice->mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];
			}
			else { // Regular MIDI input
				newBendRange = fromDevice->inputChannels[channelOrZone].bendRange;
			}

			if (newBendRange) {
				NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowForDrum(drumPressedForMIDILearn);
				if (noteRow) {
					ExpressionParamSet* expressionParams = noteRow->paramManager.getOrCreateExpressionParamSet(true);
					if (expressionParams) {
						if (!expressionParams->params[0].isAutomated()) {
							expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = newBendRange;
						}
					}
				}
			}

			if (drumPressedForMIDILearn->type == DrumType::SOUND) {
				currentSong->grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum(
				    fromDevice, (SoundDrum*)drumPressedForMIDILearn, kitPressedForMIDILearn);
			}

			goto recordDetailsOfLearnedThing;
		}

		case MidiLearn::PLAY_BUTTON:
		case MidiLearn::RECORD_BUTTON:
		case MidiLearn::TAP_TEMPO_BUTTON:
			shouldSaveSettingsAfterMidiLearn = true;
			// No break

		default:
recordDetailsOfLearnedThing:

			learnedThing->device = fromDevice;
			learnedThing->channelOrZone = channelOrZone;
			learnedThing->noteOrCC = note;
			break;

		case MidiLearn::MELODIC_INSTRUMENT_INPUT:

			uint8_t newBendRanges[2];

			ParamManager* paramManager = melodicInstrumentPressedForMIDILearn->getParamManager(
			    currentSong); // Could be NULL, e.g. for CVInstruments with no Clips

			// If we already know this incoming MIDI is on an MPE zone...
			if (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE || channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
isMPEZone:
				// Now that we've just learned a MIDI input, update bend ranges from the input device, if they were set, and no automation in activeClip.
				// Same logic can be found in InstrumentClip::changeInstrument().
				int32_t zone = channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;

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

			if (melodicInstrumentPressedForMIDILearn->type == OutputType::SYNTH) {
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

void View::ccReceivedForMIDILearn(MIDIDevice* fromDevice, int32_t channel, int32_t cc, int32_t value) {
	if (thingPressedForMidiLearn != MidiLearn::NONE) {
		deleteMidiCommandOnRelease = false;

		// For MelodicInstruments...
		if (thingPressedForMidiLearn == MidiLearn::MELODIC_INSTRUMENT_INPUT) {

			// Special case for MIDIInstruments - CCs can learn the input MIDI channel
			if (getCurrentOutputType() == OutputType::MIDI_OUT) {

				// But only if user hasn't already started learning MPE stuff... Or regular note-ons...
				if (highestMIDIChannelSeenWhileLearning < lowestMIDIChannelSeenWhileLearning) {
					learnedThing->device = fromDevice;
					learnedThing->channelOrZone = channel;
					getCurrentInstrument()->beenEdited(false);
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
	uiTimerManager.setTimer(TIMER_MIDI_LEARN_FLASH, kFastFlashTime);

	if (getRootUI()) {
		getRootUI()->midiLearnFlash();
	}

	if (midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)].containsSomething()
	    || thingPressedForMidiLearn == MidiLearn::PLAY_BUTTON) {
		indicator_leds::setLedState(IndicatorLED::PLAY, midiLearnFlashOn);
	}
	if (midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::RECORD)].containsSomething()
	    || thingPressedForMidiLearn == MidiLearn::RECORD_BUTTON) {
		indicator_leds::setLedState(IndicatorLED::RECORD, midiLearnFlashOn);
	}
	if (midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TAP)].containsSomething()
	    || thingPressedForMidiLearn == MidiLearn::TAP_TEMPO_BUTTON) {
		indicator_leds::setLedState(IndicatorLED::TAP_TEMPO, midiLearnFlashOn);
	}
}

void View::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	//this routine used to exit if shift was held, but the shift+encoder combo does not seem used anywhere else either
	// if (Buttons::isShiftButtonPressed()) {
	// 	return;
	// }

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
				ActionResult result =
				    activeModControllableModelStack.modControllable->modEncoderActionForNonExistentParam(
				        offset, whichModEncoder, modelStackWithParam);

				if (result == ActionResult::ACTIONED_AND_CAUSED_CHANGE) {
					setKnobIndicatorLevel(whichModEncoder);
				}
			}

			// Or, if normal case - an actual param
			else {
				char modelStackTempMemory[MODEL_STACK_MAX_SIZE];
				copyModelStack(modelStackTempMemory, modelStackWithParam, sizeof(ModelStackWithThreeMainThings));
				ModelStackWithThreeMainThings* tempModelStack = (ModelStackWithThreeMainThings*)modelStackTempMemory;

				int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
				int32_t lowerLimit = std::min(-64_i32, knobPos);
				int32_t newKnobPos = knobPos + offset;
				newKnobPos = std::clamp(newKnobPos, lowerLimit, 64_i32);

				Param::Kind kind = modelStackWithParam->paramCollection->getParamKind();

				//ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
				//if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a value change gets recorded
				//if newKnobPos exceeds 127, then it means current knobPos was 127 and it was increased to 128. In which case, ignore value change
				if (kind == Param::Kind::MIDI && (newKnobPos == 64)) {
					return;
				}

				//if you had selected a parameter in performance view and the parameter name
				//and current value is displayed on the screen, don't show pop-up as the display
				//already shows it
				//this checks that the param displayed on the screen in performance view
				//is the same param currently being edited with mod encoder
				bool editingParamInPerformanceView = false;
				if (getRootUI() == &performanceSessionView) {
					if (!performanceSessionView.defaultEditingMode && performanceSessionView.lastPadPress.isActive) {
						if ((kind == performanceSessionView.lastPadPress.paramKind)
						    && (modelStackWithParam->paramId == performanceSessionView.lastPadPress.paramID)) {
							editingParamInPerformanceView = true;
						}
					}
				}

				if (!editingParamInPerformanceView) {
					displayModEncoderValuePopup(kind, modelStackWithParam->paramId, newKnobPos);
				}

				if (newKnobPos == knobPos) {
					return;
				}

				//midi follow and midi feedback enabled
				//re-send midi cc because learned parameter value has changed
				sendMidiFollowFeedback(modelStackWithParam, newKnobPos);

				char newModelStackMemory[MODEL_STACK_MAX_SIZE];

				// Hack to make it so stutter can't be automated
				if (modelStackWithParam->timelineCounterIsSet()
				    && !modelStackWithParam->paramCollection->doesParamIdAllowAutomation(modelStackWithParam)) {
					copyModelStack(newModelStackMemory, modelStackWithParam, sizeof(ModelStackWithAutoParam));
					modelStackWithParam = (ModelStackWithAutoParam*)newModelStackMemory;
					modelStackWithParam->setTimelineCounter(NULL);
				}

				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Perform the actual change
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);

				if (activeModControllableModelStack.timelineCounterIsSet()) {
					bool noteTailsAllowedAfter =
					    modelStackWithParam->modControllable->allowNoteTails(tempModelStack->addSoundFlags());

					if (noteTailsAllowedBefore != noteTailsAllowedAfter) {
						if (getRootUI() && getRootUI()->isTimelineView()) {
							uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
						}
					}
				}

				if (newKnobPos == 0
				    && modelStackWithParam->paramCollection->shouldParamIndicateMiddleValue(modelStackWithParam)) {
					indicator_leds::blinkKnobIndicator(whichModEncoder);

					// Make it harder to turn that knob away from its centred position
					Encoders::timeModEncoderLastTurned[whichModEncoder] = AudioEngine::audioSampleTimer - kSampleRate;
				}
				else {
					indicator_leds::stopBlinkingKnobIndicator(whichModEncoder);
				}
			}
		}

		instrumentBeenEdited();
	}
}

void View::displayModEncoderValuePopup(Param::Kind kind, int32_t paramID, int32_t newKnobPos) {
	DEF_STACK_STRING_BUF(popupMsg, 40);

	// On OLED, display the name of the parameter on the first line of the popup
	if (display->haveOLED()) {
		const char* name = getParamDisplayName(kind, paramID);
		if (name != l10n::get(l10n::String::STRING_FOR_NONE)) {
			popupMsg.append(name);
			popupMsg.append(": ");
		}
	}

	//if turning stutter mod encoder and stutter quantize is enabled
	//display stutter quantization instead of knob position
	if (isParamQuantizedStutter(kind, paramID)) {
		if (newKnobPos < -39) { // 4ths stutter: no leds turned on
			popupMsg.append("4ths");
		}
		else if (newKnobPos < -14) { // 8ths stutter: 1 led turned on
			popupMsg.append("8ths");
		}
		else if (newKnobPos < 14) { // 16ths stutter: 2 leds turned on
			popupMsg.append("16ths");
		}
		else if (newKnobPos < 39) { // 32nds stutter: 3 leds turned on
			popupMsg.append("32nds");
		}
		else { // 64ths stutter: all 4 leds turned on
			popupMsg.append("64ths");
		}
	}
	else {
		int valueForDisplay = calculateKnobPosForDisplay(kind, paramID, newKnobPos + kKnobPosOffset);
		popupMsg.appendInt(valueForDisplay);
	}
	display->displayPopup(popupMsg.c_str());
}

//convert deluge internal knobPos range to same range as used by menu's.
int32_t View::calculateKnobPosForDisplay(Param::Kind kind, int32_t paramID, int32_t knobPos) {
	if (kind == Param::Kind::MIDI) {
		return knobPos;
	}

	float knobPosFloat = static_cast<float>(knobPos);
	float knobPosOffsetFloat = static_cast<float>(kKnobPosOffset);
	float maxKnobPosFloat = static_cast<float>(kMaxKnobPos);
	float maxMenuValueFloat = static_cast<float>(kMaxMenuValue);
	float maxMenuRelativeValueFloat = static_cast<float>(kMaxMenuRelativeValue);
	float valueForDisplayFloat;

	//calculate parameter value for display by converting 0 - 128 range to same range as menu (0 - 50)
	valueForDisplayFloat = (knobPosFloat / maxKnobPosFloat) * maxMenuValueFloat;

	//check if parameter is pan or pitch, in which case, further adjust range from 0 - 50 to -25 to +25
	if (isParamPan(kind, paramID) || isParamPitch(kind, paramID)) {
		valueForDisplayFloat = valueForDisplayFloat - maxMenuRelativeValueFloat;
	}

returnValue:
	return static_cast<int32_t>(std::round(valueForDisplayFloat));
}

//check if Parameter is Stutter Rate and if Quantized Stutter Community Feature is enabled
bool View::isParamQuantizedStutter(Param::Kind kind, int32_t paramID) {
	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::QuantizedStutterRate) != RuntimeFeatureStateToggle::On) {
		return false;
	}
	if ((kind == Param::Kind::UNPATCHED_GLOBAL || kind == Param::Kind::UNPATCHED_SOUND)
	    && paramID == Param::Unpatched::Shared::STUTTER_RATE) {
		return true;
	}
	return false;
}

bool View::isParamPan(Param::Kind kind, int32_t paramID) {
	if ((kind == Param::Kind::PATCHED && paramID == Param::Local::PAN)
	    || (kind == Param::Kind::UNPATCHED_GLOBAL && paramID == Param::Unpatched::GlobalEffectable::PAN)) {
		return true;
	}

	return false;
}

bool View::isParamPitch(Param::Kind kind, int32_t paramID) {
	if ((kind == Param::Kind::PATCHED && paramID == Param::Local::PITCH_ADJUST)
	    || (kind == Param::Kind::PATCHED && paramID == Param::Local::OSC_A_PITCH_ADJUST)
	    || (kind == Param::Kind::PATCHED && paramID == Param::Local::OSC_B_PITCH_ADJUST)
	    || (kind == Param::Kind::PATCHED && paramID == Param::Local::MODULATOR_0_PITCH_ADJUST)
	    || (kind == Param::Kind::PATCHED && paramID == Param::Local::MODULATOR_1_PITCH_ADJUST)
	    || (kind == Param::Kind::UNPATCHED_GLOBAL && paramID == Param::Unpatched::GlobalEffectable::PITCH_ADJUST)) {
		return true;
	}

	return false;
}

void View::instrumentBeenEdited() {
	if (activeModControllableModelStack.timelineCounterIsSet()) {
		activeModControllableModelStack.getTimelineCounter()->instrumentBeenEdited();
	}
}

void View::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// If the learn button is pressed, user is trying to copy or paste, and the fact that we've ended up here means they can't
	if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		if (display->have7SEG()) {
			if (on) {
				display->displayPopup("CANT");
			}
		}
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
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED));
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

	//don't update knob indicator levels when you're in automation editor
	if ((getCurrentUI() == &automationInstrumentClipView) && !automationInstrumentClipView.isOnAutomationOverview()) {
		return;
	}

	if (activeModControllableModelStack.modControllable) {
		for (int32_t whichModEncoder = 0; whichModEncoder < NUM_LEVEL_INDICATORS; whichModEncoder++) {
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

	int32_t knobPos;

	if (modelStackWithParam->autoParam) {
		int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
		ParamCollection* paramCollection = modelStackWithParam->paramCollection;
		knobPos = paramCollection->paramValueToKnobPos(value, modelStackWithParam);
		if (knobPos < -64) {
			knobPos = -64;
		}
		else if (knobPos > 64) {
			knobPos = 64;
		}

		if (isParamQuantizedStutter(paramCollection->getParamKind(), modelStackWithParam->paramId)
		    && !isUIModeActive(UI_MODE_STUTTERING)) {
			if (knobPos < -39) { // 4ths stutter: no leds turned on
				knobPos = -64;
			}
			else if (knobPos < -14) { // 8ths stutter: 1 led turned on
				knobPos = -32;
			}
			else if (knobPos < 14) { // 16ths stutter: 2 leds turned on
				knobPos = 0;
			}
			else if (knobPos < 39) { // 32nds stutter: 3 leds turned on
				knobPos = 32;
			}
			else { // 64ths stutter: all 4 leds turned on
				knobPos = 64;
			}
		}
	}
	else {
		knobPos =
		    modelStackWithParam->modControllable->getKnobPosForNonExistentParam(whichModEncoder, modelStackWithParam);
	}

	// Quantized Stutter FX
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

	//ignore modButtonAction when in the Automation View Automation Editor
	if ((getRootUI() == &automationInstrumentClipView) && !automationInstrumentClipView.isOnAutomationOverview()) {
		return;
	}

	pretendModKnobsUntouchedForAWhile();

	if (activeModControllableModelStack.modControllable) {
		if (on) {

			if (isUIModeWithinRange(modButtonUIModes) || (getRootUI() == &performanceSessionView)) {
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
		}
	}
}

void View::setModLedStates() {

	bool itsTheSong = activeModControllableModelStack.getTimelineCounterAllowNull() == currentSong
	                  || (!activeModControllableModelStack.timelineCounterIsSet()
	                      && (getRootUI() == &sessionView || getRootUI() == &arrangerView));

	bool itsAClip = activeModControllableModelStack.timelineCounterIsSet()
	                && activeModControllableModelStack.getTimelineCounter() != currentSong;

	bool affectEntire = getRootUI() && getRootUI()->getAffectEntire();
	if (!itsTheSong) {
		if (getRootUI() != &instrumentClipView && getRootUI() != &automationInstrumentClipView
		    && getRootUI() != &keyboardScreen) {
			affectEntire = true;
		}
		else {
			affectEntire = getCurrentInstrumentClip()->affectEntire;
		}
	}
	indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, affectEntire);

	if (itsTheSong) {
		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
	}
	else {
		if (getRootUI() == &sessionView) {
			Clip* clip = sessionView.getClipForLayout();

			if (clip) {
				if (clip->output->type == OutputType::AUDIO) {
					if ((((AudioClip*)clip)->onAutomationAudioClipView)) {
						goto setBlinkLED;
					}
				}
				else {
					if ((((InstrumentClip*)clip)->onAutomationInstrumentClipView)) {
						goto setBlinkLED;
					}
				}
			}
		}
		else if (getRootUI() == &arrangerView) {
			Output* output = arrangerView.outputsOnScreen[arrangerView.yPressedEffective];

			if (output) {
				if (output->type == OutputType::AUDIO) {
					if (((AudioClip*)currentSong->getClipWithOutput(output))->onAutomationAudioClipView) {
						goto setBlinkLED;
					}
				}
				else {
					if (((InstrumentClip*)currentSong->getClipWithOutput(output))->onAutomationInstrumentClipView) {
						goto setBlinkLED;
					}
				}
			}
		}
		else if (getRootUI() == &keyboardScreen) {
			if (getCurrentInstrumentClip()->onAutomationInstrumentClipView) {
				goto setBlinkLED;
			}
		}
		else if (getRootUI() == &automationInstrumentClipView) {
			goto setBlinkLED;
		}

		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, true);
		goto setNextLED;

setBlinkLED:

		indicator_leds::blinkLed(IndicatorLED::CLIP_VIEW);
		goto setNextLED;
	}

setNextLED:
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
	int32_t modKnobMode = -1;
	if (activeModControllableModelStack.modControllable) {
		uint8_t* modKnobModePointer = activeModControllableModelStack.modControllable->getModKnobMode();
		if (modKnobModePointer) {
			modKnobMode = *modKnobModePointer;
		}
	}

	for (int32_t i = 0; i < kNumModButtons; i++) {
		bool on = (i == modKnobMode);
		//if you're in the Automation View Automation Editor, turn off Mod LED's
		if ((getRootUI() == &automationInstrumentClipView) && !automationInstrumentClipView.isOnAutomationOverview()) {
			indicator_leds::setLedState(indicator_leds::modLed[i], false);
		}
		else {
			indicator_leds::setLedState(indicator_leds::modLed[i], on);
		}
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

		if (!uiTimerManager.isTimerSet(TIMER_SEND_MIDI_FEEDBACK_FOR_AUTOMATION)) {
			uiTimerManager.setTimer(TIMER_SEND_MIDI_FEEDBACK_FOR_AUTOMATION, 25);
		}
	}
}

void View::sendMidiFollowFeedback(ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos, bool isAutomation) {
	int32_t channel = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::PARAM)].channelOrZone;
	if ((channel != MIDI_CHANNEL_NONE) && midiEngine.midiFollowFeedback
	    && activeModControllableModelStack.modControllable) {
		if (modelStackWithParam && modelStackWithParam->autoParam) {
			Param::Kind kind = modelStackWithParam->paramCollection->getParamKind();
			int32_t ccNumber = midiFollow.getCCFromParam(kind, modelStackWithParam->paramId);
			if (ccNumber != MIDI_CC_NONE) {
				((ModControllableAudio*)activeModControllableModelStack.modControllable)
				    ->sendCCForMidiFollowFeedback(channel, ccNumber, knobPos);
			}
		}
		else {
			((ModControllableAudio*)activeModControllableModelStack.modControllable)
			    ->sendCCWithoutModelStackForMidiFollowFeedback(channel, isAutomation);
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

	//midi follow and midi feedback enabled
	//re-send midi cc's because learned parameter values may have changed
	sendMidiFollowFeedback();
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

void View::setModRegion(uint32_t pos, uint32_t length, int32_t noteRowId) {

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
	    AudioEngine::audioSampleTimer - kSampleRate;
}

void View::cycleThroughReverbPresets() {

	int32_t currentRoomSize = AudioEngine::reverb.getroomsize() * 50;
	int32_t currentDampening = AudioEngine::reverb.getdamp() * 50;

	// See which preset we're the closest to currently
	int32_t lowestDifferentness = 1000;
	int32_t currentPreset;
	for (int32_t p = 0; p < NUM_PRESET_REVERBS; p++) {
		int32_t differentness =
		    std::abs(currentRoomSize - presetReverbRoomSize[p]) + std::abs(currentDampening - presetReverbDampening[p]);
		if (differentness < lowestDifferentness) {
			lowestDifferentness = differentness;
			currentPreset = p;
		}
	}

	int32_t newPreset = currentPreset + 1;
	if (newPreset >= NUM_PRESET_REVERBS) {
		newPreset = 0;
	}

	AudioEngine::reverb.setroomsize((float)presetReverbRoomSize[newPreset] / 50);
	AudioEngine::reverb.setdamp((float)presetReverbDampening[newPreset] / 50);

	display->displayPopup(deluge::l10n::get(presetReverbNames[newPreset]));
}

// If OLED, must make sure deluge::hid::display::OLED::sendMainImage() gets called after this.
void View::displayOutputName(Output* output, bool doBlink, Clip* clip) {

	int32_t channel, channelSuffix;
	bool editedByUser = true;
	if (output->type != OutputType::AUDIO) {
		Instrument* instrument = (Instrument*)output;
		editedByUser = !instrument->existsOnCard;
		switch (output->type) {
		case OutputType::MIDI_OUT:
			channelSuffix = ((MIDIInstrument*)instrument)->channelSuffix;
			// No break

		case OutputType::CV:
			channel = ((NonAudioInstrument*)instrument)->channel;
			break;
		}
	}

	drawOutputNameFromDetails(output->type, channel, channelSuffix, output->name.get(), editedByUser, doBlink, clip);
}

// If OLED, must make sure deluge::hid::display::OLED::sendMainImage() gets called after this.
void View::drawOutputNameFromDetails(OutputType outputType, int32_t channel, int32_t channelSuffix, char const* name,
                                     bool editedByUser, bool doBlink, Clip* clip) {
	if (doBlink) {
		using namespace indicator_leds;
		LED led;

		if (outputType == OutputType::SYNTH) {
			led = LED::SYNTH;
		}
		else {
			setLedState(LED::SYNTH, false);
		}

		if (outputType == OutputType::KIT) {
			led = LED::KIT;
		}
		else {
			setLedState(LED::KIT, false);
		}

		if (outputType == OutputType::MIDI_OUT) {
			led = LED::MIDI;
		}
		else {
			setLedState(LED::MIDI, false);
		}

		if (outputType == OutputType::CV) {
			led = LED::CV;
		}
		else {
			setLedState(LED::CV, false);
		}

		if (outputType != OutputType::AUDIO) {
			blinkLed(led);
		}

		InstrumentClip* clip = NULL;
		if (clip && clip->type == CLIP_TYPE_INSTRUMENT) {
			clip = (InstrumentClip*)clip;
		}

		setLedState(LED::KEYBOARD, (clip && clip->onKeyboardScreen));
		setLedState(LED::SCALE_MODE, (clip && clip->inScaleMode && clip->output->type != OutputType::KIT));
		setLedState(LED::CROSS_SCREEN_EDIT, (clip && clip->wrapEditing));
	}

	//hook to render display for OLED and 7SEG when in Automation Instrument Clip View
	if (getCurrentUI() == &automationInstrumentClipView) {
		if (!automationInstrumentClipView.isOnAutomationOverview()) {
			automationInstrumentClipView.displayAutomation(true, !display->have7SEG());
		}
		else {
			automationInstrumentClipView.renderDisplay();
		}
		return;
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();
		char const* outputTypeText;
		switch (outputType) {
		case OutputType::SYNTH:
			outputTypeText = "Synth";
			break;
		case OutputType::KIT:
			outputTypeText = "Kit";
			break;
		case OutputType::MIDI_OUT:
			outputTypeText = (channel < 16) ? "MIDI channel" : "MPE zone";
			break;
		case OutputType::CV:
			outputTypeText = "CV / gate channel";
			break;
		case OutputType::AUDIO:
			outputTypeText = "Audio track";
			break;
		default:
			__builtin_unreachable();
		}

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
		deluge::hid::display::OLED::drawStringCentred(outputTypeText, yPos,
		                                              deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);
	}

	char buffer[12];
	char const* nameToDraw = nullptr;

	if (name && name[0]) {
		if (display->haveOLED()) {
			nameToDraw = name;
oledDrawString:
#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 32;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 21;
#endif

			int32_t textSpacingX = kTextTitleSpacingX;
			int32_t textSpacingY = kTextTitleSizeY;

			int32_t textLength = strlen(name);
			int32_t stringLengthPixels = textLength * textSpacingX;
			if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
				deluge::hid::display::OLED::drawStringCentred(nameToDraw, yPos,
				                                              deluge::hid::display::OLED::oledMainImage[0],
				                                              OLED_MAIN_WIDTH_PIXELS, textSpacingX, textSpacingY);
			}
			else {
				deluge::hid::display::OLED::drawString(nameToDraw, 0, yPos,
				                                       deluge::hid::display::OLED::oledMainImage[0],
				                                       OLED_MAIN_WIDTH_PIXELS, textSpacingX, textSpacingY);
				deluge::hid::display::OLED::setupSideScroller(0, name, 0, OLED_MAIN_WIDTH_PIXELS, yPos,
				                                              yPos + textSpacingY, textSpacingX, textSpacingY, false);
			}
		}
		else {
			bool andAHalf;
			if (display->getEncodedPosFromLeft(99999, name, &andAHalf) > kNumericDisplayLength) { // doBlink &&
				display->setScrollingText(name, 0, kInitialFlashTime + kFlashTime);
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

				display->setText(name, alignRight, dotPos, doBlink);
			}
		}
	}
	else if (outputType == OutputType::MIDI_OUT) {
		if (display->haveOLED()) {
			if (channel < 16) {
				slotToString(channel + 1, channelSuffix, buffer, 1);
				goto oledOutputBuffer;
			}
			else {
				nameToDraw = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
				goto oledDrawString;
			}
		}
		else {
			if (channel < 16) {
				display->setTextAsSlot(channel + 1, channelSuffix, false, doBlink);
			}
			else {
				char const* text = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
				display->setText(text, false, 255, doBlink);
			}
		}
	}
	else if (outputType == OutputType::CV) {
		if (display->haveOLED()) {
			intToString(channel + 1, buffer);
oledOutputBuffer:
			nameToDraw = buffer;
			goto oledDrawString;
		}
		else {
			display->setTextAsNumber(channel + 1, 255, doBlink);
		}
	}
}

void View::navigateThroughAudioOutputsForAudioClip(int32_t offset, AudioClip* clip, bool doBlink) {

	AudioEngine::logAction("navigateThroughPresets");

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	// Work out availabilityRequirement. But we don't in this case need to think about whether the Output can be "replaced" - that's for InstrumentClips
	Availability availabilityRequirement;
	currentSong->canOldOutputBeReplaced(clip, &availabilityRequirement);

	if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_HAS_INSTANCES_IN_ARRANGER));
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
	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage();
	}

	setActiveModControllableTimelineCounter(clip); // Necessary? Does ParamManager get moved over too?
}

void View::navigateThroughPresetsForInstrumentClip(int32_t offset, ModelStackWithTimelineCounter* modelStack,
                                                   bool doBlink) {

	AudioEngine::logAction("navigateThroughPresets");

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	int32_t oldSubMode = currentUIMode; // We may have been holding down a clip in Session View

	actionLogger.deleteAllLogs(); // Can't undo past this!

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	OutputType outputType = clip->output->type;

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E057", "H057");

	// Work out availabilityRequirement. This can't change as presets are navigated through... I don't think?
	Availability availabilityRequirement;
	bool oldInstrumentCanBeReplaced = modelStack->song->canOldOutputBeReplaced(clip, &availabilityRequirement);

	bool shouldReplaceWholeInstrument;

	Instrument* newInstrument;
	Instrument* oldInstrument = (Instrument*)clip->output;

	// If we're in MIDI or CV mode, easy - just change the channel
	if (outputType == OutputType::MIDI_OUT || outputType == OutputType::CV) {

		NonAudioInstrument* oldNonAudioInstrument = (NonAudioInstrument*)oldInstrument;
		int32_t newChannel = oldNonAudioInstrument->channel;
		int32_t newChannelSuffix;
		if (outputType == OutputType::MIDI_OUT) {
			newChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
		}

		// TODO: the contents of these badly wants to be replaced with how I did it in changeOutputType()!

		// CV
		if (outputType == OutputType::CV) {
			while (true) {
				newChannel = (newChannel + offset) & (NUM_CV_CHANNELS - 1);

				if (newChannel == oldNonAudioInstrument->channel) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_UNUSED_CHANNELS));
					return;
				}

				if (availabilityRequirement == Availability::ANY) {
					break;
				}
				else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
					if (!modelStack->song->doesNonAudioSlotHaveActiveClipInSession(outputType, newChannel)) {
						break;
					}
				}
				else if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
					if (!modelStack->song->getInstrumentFromPresetSlot(outputType, newChannel, -1, NULL, NULL, false)) {
						break;
					}
				}
			}
		}

		// Or MIDI
		else {

			int32_t oldChannel = newChannel;

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
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_UNUSED_CHANNELS));
					return;
				}

				if (availabilityRequirement == Availability::ANY) {
					break;
				}
				else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
					if (!modelStack->song->doesNonAudioSlotHaveActiveClipInSession(outputType, newChannel,
					                                                               newChannelSuffix)) {
						break;
					}
				}
				else if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
					if (!modelStack->song->getInstrumentFromPresetSlot(outputType, newChannel, newChannelSuffix, NULL,
					                                                   NULL, false)) {
						break;
					}
				}
			}

			oldNonAudioInstrument->channel = oldChannel; // Put it back
		}

		newInstrument =
		    modelStack->song->getInstrumentFromPresetSlot(outputType, newChannel, newChannelSuffix, NULL, NULL, false);

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
			if (outputType == OutputType::MIDI_OUT) {
				((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix = newChannelSuffix;
			}

			newInstrument = oldNonAudioInstrument;
		}

		// Otherwise...
		else {

			bool instrumentAlreadyInSong = (newInstrument != NULL);

			// If an Instrument doesn't yet exist for the new channel we're gonna use...
			if (!newInstrument) {
				if (outputType == OutputType::MIDI_OUT) {
					newInstrument = modelStack->song->grabHibernatingMIDIInstrument(newChannel, newChannelSuffix);
					if (newInstrument) {
						goto gotAnInstrument;
					}
				}
				newInstrument = storageManager.createNewNonAudioInstrument(outputType, newChannel, newChannelSuffix);
				if (!newInstrument) {
					display->displayError(ERROR_INSUFFICIENT_RAM);
					return;
				}

				// We just allocated a brand new Instrument in RAM. If MIDI, copy knob assignments from old Instrument
				if (outputType == OutputType::MIDI_OUT) {
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

			int32_t error = clip->changeInstrument(modelStack, newInstrument, NULL,
			                                       InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, NULL, true);
			// TODO: deal with errors

			if (!instrumentAlreadyInSong) {
				modelStack->song->addOutput(newInstrument);
			}
		}

		displayOutputName(newInstrument, doBlink);
		if (display->haveOLED()) {
			deluge::hid::display::OLED::sendMainImage();
		}
	}

	// Or if we're on a Kit or Synth...
	else {

		PresetNavigationResult results =
		    loadInstrumentPresetUI.doPresetNavigation(offset, oldInstrument, availabilityRequirement, false);
		if (results.error == NO_ERROR_BUT_GET_OUT) {
getOut:
			display->removeWorkingAnimation();
			return;
		}
		else if (results.error) {
			display->displayError(results.error);
			goto getOut;
		}

		newInstrument = results.fileItem->instrument;
		bool instrumentAlreadyInSong = results.fileItem->instrumentAlreadyInSong;
		Browser::emptyFileItems();

		// For Kits, ensure that every SoundDrum has a ParamManager somewhere
#if ALPHA_OR_BETA_VERSION
		if (newInstrument->type == OutputType::KIT) {
			Kit* kit = (Kit*)newInstrument;
			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					if (!modelStack->song->getBackedUpParamManagerPreferablyWithClip(
					        soundDrum, NULL)) { // If no backedUpParamManager...
						if (!modelStack->song->findParamManagerForDrum(
						        kit, soundDrum)) { // If no ParamManager with a NoteRow somewhere...

							if (results.loadedFromFile) {
								FREEZE_WITH_ERROR("E103");
							}
							else if (instrumentAlreadyInSong) {
								FREEZE_WITH_ERROR("E104");
							}
							else {
								// Sven got - very rare! This means Kit was hibernating, I guess.
								FREEZE_WITH_ERROR("E105");
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

			int32_t error = clip->changeInstrument(modelStack, newInstrument, NULL,
			                                       InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, NULL, true);
			// TODO: deal with errors!

			if (!instrumentAlreadyInSong) {
				modelStack->song->addOutput(newInstrument);
			}
		}

		// Kit-specific stuff
		if (outputType == OutputType::KIT) {
			clip->ensureScrollWithinKitBounds();
			((Kit*)newInstrument)->selectedDrum = NULL;
		}

		if (getCurrentUI() == &instrumentClipView || getCurrentUI() == &automationInstrumentClipView) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			instrumentClipView.recalculateColours();
		}

		if (getCurrentUI() == &instrumentClipView) {
			uiNeedsRendering(&instrumentClipView);
		}

		else if (getCurrentUI() == &automationInstrumentClipView) {
			uiNeedsRendering(&automationInstrumentClipView);
		}

		display->removeLoadingAnimation();
	}

	instrumentChanged(modelStack, newInstrument);

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager(
	    "E058",
	    "H058"); // I got this during limited-RAM testing. Maybe there wasn't enough RAM to create the ParamManager or store its backup?
}

// Returns whether success
bool View::changeOutputType(OutputType newOutputType, ModelStackWithTimelineCounter* modelStack, bool doBlink) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	OutputType oldOutputType = clip->output->type;
	if (oldOutputType == newOutputType) {
		return false;
	}

	Instrument* newInstrument = clip->changeOutputType(modelStack, newOutputType);
	if (!newInstrument) {
		return false;
	}

	// Do a redraw. Obviously the Clip is the same
	setActiveModControllableTimelineCounter(clip);
	displayOutputName(newInstrument, doBlink);
	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage();
	}

	return true;
}

void View::instrumentChanged(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument) {

	((Clip*)modelStack->getTimelineCounter())->outputChanged(modelStack, newInstrument);
	// Do a redraw. Obviously the Clip is the same
	setActiveModControllableTimelineCounter(modelStack->getTimelineCounter());
}

void View::getClipMuteSquareColour(Clip* clip, uint8_t thisColour[], bool dimInactivePads, bool allowMIDIFlash) {

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
	if (allowMIDIFlash && midiLearnFlashOn && clip->muteMIDICommand.containsSomething()) {
		thisColour[0] = midiCommandColour.r;
		thisColour[1] = midiCommandColour.g;
		thisColour[2] = midiCommandColour.b;
		return;
	}

	if (clipArmFlashOn && clip->armState != ArmState::OFF) {
		thisColour[0] = 0;
		thisColour[1] = 0;
		thisColour[2] = 0;
	}

	// If it's soloed or armed to solo, blue
	else if (clip->soloingInSessionMode || clip->armState == ArmState::ON_TO_SOLO) {
		menu_item::soloColourMenu.getRGB(thisColour);
	}

	// Or if not soloing...
	else {
		if (clip->launchStyle == LAUNCH_STYLE_DEFAULT) {
			// If it's stopped, red.
			if (!clip->activeIfNoSolo) {
				if (dimInactivePads) {
					thisColour[0] = 20;
					thisColour[1] = 20;
					thisColour[2] = 20;
				}
				else {
					menu_item::stoppedColourMenu.getRGB(thisColour);
				}
			}

			// Or, green.
			else {
				menu_item::activeColourMenu.getRGB(thisColour);
			}
		}
		else {
			// If it's stopped, orange.
			if (!clip->activeIfNoSolo) {
				if (dimInactivePads) {
					thisColour[0] = 10;
					thisColour[1] = 7;
					thisColour[2] = 3;
				}
				else {
					thisColour[0] = 255;
					thisColour[1] = 64;
					thisColour[2] = 0;
				}
			}

			// Or, cyan.
			else {
				thisColour[0] = 0;
				thisColour[1] = 255;
				thisColour[2] = 255;
			}
		}

		if (currentSong->getAnyClipsSoloing()) {
			dimColour(thisColour);
		}
	}

	// If user assigning MIDI controls and has this Clip selected, flash to half brightness
	if (allowMIDIFlash && midiLearnFlashOn && learnedThing == &clip->muteMIDICommand) {
		thisColour[0] >>= 1;
		thisColour[1] >>= 1;
		thisColour[2] >>= 1;
	}
}

ActionResult View::clipStatusPadAction(Clip* clip, bool on, int32_t yDisplayIfInSessionView) {

	switch (currentUIMode) {
	case UI_MODE_MIDI_LEARN:
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
		if (on && Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			clip->armedForRecording = !clip->armedForRecording;
			sessionView
			    .timerCallback(); // Get into UI_MODE_VIEWING_RECORD_ARMING. TODO: this needs doing properly - what if we're in a Clip view?
			break;
		}
		// No break
	case UI_MODE_CLIP_PRESSED_IN_SONG_VIEW:
	case UI_MODE_STUTTERING:
	case UI_MODE_HOLDING_STATUS_PAD:
		if (on) {
			enterUIMode(UI_MODE_HOLDING_STATUS_PAD);
			context_menu::launchStyle.clip = clip;
			sessionView.performActionOnPadRelease = false; // Even though there's a chance we're not in session view
			session.toggleClipStatus(clip, NULL, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_STATUS_PAD);
		}
		break;

#ifdef soloButtonX
	case UI_MODE_SOLO_BUTTON_HELD:
#else
	case UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON:
#endif
		if (on) {
			sessionView.performActionOnPadRelease = false; // Even though there's a chance we're not in session view
			session.soloClipAction(clip, kInternalButtonPressLatency);
		}
		break;
	}

	return ActionResult::DEALT_WITH;
}

void View::flashPlayEnable() {
	uiTimerManager.setTimer(TIMER_PLAY_ENABLE_FLASH, kFastFlashTime);
}

void View::flashPlayDisable() {
	clipArmFlashOn = false;
	uiTimerManager.unsetTimer(TIMER_PLAY_ENABLE_FLASH);

	if (getRootUI() == &sessionView) {
		uiNeedsRendering(&sessionView, 0, 0xFFFFFFFF);
	}
#ifdef currentClipStatusButtonX
	else if (getRootUI()->toClipMinder()) {
		drawCurrentClipPad(getCurrentClip());
	}
#endif
}

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = setupModelStack(modelStackMemory);
*/
