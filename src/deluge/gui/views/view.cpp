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
#include "dsp/reverb/reverb.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/context_menu/clear_song.h"
#include "gui/context_menu/clip_settings/clip_settings.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "lib/printf.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/consequence/consequence.h"
#include "model/drum/drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/timeline_counter.h"
#include "modulation/arpeggiator_rhythms.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/retrospective/retrospective_buffer.h"
#include "processing/retrospective/retrospective_handler.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"

namespace params = deluge::modulation::params;
namespace encoders = deluge::hid::encoders;
using namespace deluge;
using namespace gui;

View view{};

extern GlobalMIDICommand pendingGlobalMIDICommand;

View::View() {
	midiLearnFlashOn = false;

	deleteMidiCommandOnRelease = false;

	learnedThing = nullptr;

	memset(&activeModControllableModelStack, 0, sizeof(activeModControllableModelStack));

	modLength = 0;
	modPos = 0xFFFFFFFF;
	clipArmFlashOn = false;
	displayVUMeter = false;
	renderedVUMeter = false;
	cachedMaxYDisplayForVUMeterL = 255;
	cachedMaxYDisplayForVUMeterR = 255;
}

void View::focusRegained() {
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	if (auto* timeline = getCurrentUI()->toTimelineView()) {
		timeline->setTripletsLEDState();
	}

	indicator_leds::setLedState(IndicatorLED::LOAD, false);
	indicator_leds::setLedState(IndicatorLED::SAVE, false);

	indicator_leds::setLedState(IndicatorLED::LEARN, false);

	// when switching between UI's we want to start with a fresh VU meter render
	renderedVUMeter = false;
	cachedMaxYDisplayForVUMeterL = 255;
	cachedMaxYDisplayForVUMeterR = 255;
}

extern GlobalMIDICommand pendingGlobalMIDICommandNumClustersWritten;

ActionResult View::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	GlobalMIDICommand newGlobalMidiCommand;

	// Tap tempo button. Shouldn't move this to MatrixDriver, because this code can put us in tapTempo mode, and other
	// UIs aren't built to handle this
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
		else if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_RECORD_COUNT_IN) {
			if (on) {
				// If shift button, toggle metronome
				if (Buttons::isShiftButtonPressed()) {
					playbackHandler.toggleMetronomeStatus();
				}
				// Otherwise, normal - tap tempo, but not during record count in
				else if (currentUIMode == UI_MODE_NONE) {
					bool useNormalTapTempoBehaviour =
					    (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AlternativeTapTempoBehaviour)
					     == RuntimeFeatureStateToggle::Off);
					playbackHandler.tapTempoButtonPress(useNormalTapTempoBehaviour);
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
			startMIDILearn();
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
		    && !((getRootUI() == &performanceView) && Buttons::isButtonPressed(deluge::hid::button::KEYBOARD))) {
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
		    && !((getRootUI() == &performanceView) && Buttons::isButtonPressed(deluge::hid::button::KEYBOARD))) {
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

						// Need to redraw everything if no success, because the LoadSongUI does some drawing before even
						// determining whether it can start successfully
						if (!success) {
							//((ViewScreen*)getCurrentUI())->renderAllRows();
							// beginSession();
						}
					}
					else {
						indicator_leds::setLedState(IndicatorLED::LOAD, false);
					}
				}
				else if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING) {
					indicator_leds::setLedState(IndicatorLED::LOAD, false);
				}
			}
		}
	}

	// Sync-scaling button - can be repurposed as Fill Mode in community settings
	// Also used as RECORD + SYNC_SCALING combo for retrospective sampler
	else if (b == SYNC_SCALING) {
		// RECORD + SYNC_SCALING combo - trigger retrospective save if enabled
		// Don't trigger if threshold recording mode popup is active (RECORD shows that popup)
		if (on && Buttons::isButtonPressed(RECORD) && !display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)
		    && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSampler)
		    && retrospectiveBuffer.hasAudio()) {
			Buttons::recordButtonPressUsedUp = true; // Prevent normal record action on release
			handleRetrospectiveSave();
			return ActionResult::DEALT_WITH;
		}
		else if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction)
		          == RuntimeFeatureStateSyncScalingAction::Fill)) {
			currentSong->changeFillMode(on);
		}
		else if (on && currentUIMode == UI_MODE_NONE) {

			if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
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
				currentSong->setInputTickScaleClip(nullptr);
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

				// Here we'll take advantage of the pending command system which has to exist for these commands for
				// their MIDI-triggered case anyway. In the future, maybe a lot more commands should pend in the same
				// way?
				pendingGlobalMIDICommand =
				    Buttons::isShiftButtonPressed() ? GlobalMIDICommand::REDO : GlobalMIDICommand::UNDO;
				pendingGlobalMIDICommandNumClustersWritten = GlobalMIDICommand::PLAYBACK_RESTART; // Bug hunting.
				playbackHandler.slowRoutine(); // Do it now if not reading card.
			}
#endif
		}
	}

#ifdef undoButtonX
	// Undo button
	else if (b == undo) {
		newGlobalMidiCommand = GlobalMIDICommand::UNDO;
possiblyRevert:
		if (on) {
			if (actionLogger.allowedToDoReversion()) {

				// Here we'll take advantage of the pending command system which has to exist for these commands for
				// their MIDI-triggered case anyway. In the future, maybe a lot more commands should pend in the same
				// way?
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

			if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
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

void View::startMIDILearn() {
	if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN) {
		thingPressedForMidiLearn = MidiLearn::NONE;
		shouldSaveSettingsAfterMidiLearn = false;
		currentUIMode = UI_MODE_MIDI_LEARN;
		midiLearnFlash();
		indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1);
	}
}

void View::endMIDILearn() {
	if (shouldSaveSettingsAfterMidiLearn) {
		if (!AudioEngine::audioRoutineLocked) {
			FlashStorage::writeSettings(); // Rare case where we could have been called during audio routine
		}
	}
	uiTimerManager.unsetTimer(TimerName::MIDI_LEARN_FLASH);
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
		indicator_leds::setLedState(IndicatorLED::SYNC_SCALING, currentSong->getSyncScalingClip() != nullptr);
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
		kitPressedForMIDILearn = kit; // Having this makes it possible to search much faster when we call
		                              // grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForDrum()
	}

	else if (thingPressedForMidiLearn == MidiLearn::DRUM_INPUT) {
		if (deleteMidiCommandOnRelease) {
			learnedThing->clear();
			getCurrentInstrument()->beenEdited(false);
		}
		endMidiLearnPressSession();
	}
}

void View::instrumentMidiLearnPadPressed(bool on, Instrument* instrument) {
	if (on) {
		endMidiLearnPressSession(MidiLearn::INSTRUMENT_INPUT);
		deleteMidiCommandOnRelease = true;
		learnedThing = &instrument->midiInput;
		instrumentPressedForMIDILearn = instrument;
	}

	else if (thingPressedForMidiLearn == MidiLearn::INSTRUMENT_INPUT) {
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
	// explicit fallthrough cases
	case MidiLearn::CLIP:
	case MidiLearn::NONE:
	case MidiLearn::NOTEROW_MUTE:
	case MidiLearn::SECTION:
	case MidiLearn::INSTRUMENT_INPUT:
	case MidiLearn::DRUM_INPUT:;
	}

	learnedThing = nullptr;

	// And, store the actual change
	thingPressedForMidiLearn = newThingPressed;

	// Hook point for specificMidiDevice
	iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook::HOOK_ON_MIDI_LEARN);
}

void View::noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channelOrZone, int32_t note, int32_t velocity) {
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
				newBendRange = cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];
			}
			else { // Regular MIDI input
				newBendRange = cable.inputChannels[channelOrZone].bendRange;
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
				currentSong->grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForDrum(
				    cable, (SoundDrum*)drumPressedForMIDILearn, kitPressedForMIDILearn);
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

			learnedThing->cable = &cable;
			learnedThing->channelOrZone = channelOrZone;
			learnedThing->noteOrCC = note;
			break;

		case MidiLearn::INSTRUMENT_INPUT:

			uint8_t newBendRanges[2];

			ParamManager* paramManager = instrumentPressedForMIDILearn->getParamManager(
			    currentSong); // Could be NULL, e.g. for CVInstruments with no Clips

			// If we already know this incoming MIDI is on an MPE zone...
			if (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE || channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
isMPEZone:
				// Now that we've just learned a MIDI input, update bend ranges from the input device, if they were set,
				// and no automation in activeClip. Same logic can be found in InstrumentClip::changeInstrument().
				int32_t zone = channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;

				newBendRanges[BEND_RANGE_MAIN] = cable.mpeZoneBendRanges[zone][BEND_RANGE_MAIN];
				newBendRanges[BEND_RANGE_FINGER_LEVEL] = cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];

				if (newBendRanges[BEND_RANGE_FINGER_LEVEL]) {
					InstrumentClip* clip = (InstrumentClip*)instrumentPressedForMIDILearn->getActiveClip();
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
				// If still here, it's not MPE. Now that we know that, see if we want to apply a stored bend range for
				// the input MIDI channel of the device
				newBendRanges[BEND_RANGE_MAIN] = cable.inputChannels[channelOrZone].bendRange;
			}

			if (newBendRanges[BEND_RANGE_MAIN]) {
				if (paramManager) { // Could be NULL, e.g. for CVInstruments with no Clips
					ExpressionParamSet* expressionParams = paramManager->getOrCreateExpressionParamSet();
					if (expressionParams && !expressionParams->params[0].isAutomated()) {
						expressionParams->bendRanges[BEND_RANGE_MAIN] = newBendRanges[BEND_RANGE_MAIN];
					}
				}
			}
			// In a perfect world, we'd also update CVInstrument::cachedBendRanges[]. But that'd only make a difference
			// if it had no Clips.

			// We need to reset the expression params, in case they've gotten stuck. This was mostly prone to happening
			// when doing the "learn MPE input" multi-finger trick.
			clearMelodicInstrumentMonoExpressionIfPossible();

			learnedThing->channelOrZone = channelOrZone;
			learnedThing->cable = &cable;
			learnedThing->noteOrCC = note;                    // used for low note in kits
			instrumentPressedForMIDILearn->beenEdited(false); // Why again?

			if (instrumentPressedForMIDILearn->type == OutputType::SYNTH) {
				currentSong->grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForInstrument(
				    cable, (SoundInstrument*)instrumentPressedForMIDILearn);
			}

			break;
		}
	}
}

void View::clearMelodicInstrumentMonoExpressionIfPossible() {

	ParamManager* paramManager = instrumentPressedForMIDILearn->getParamManager(
	    currentSong); // Could be NULL, e.g. for CVInstruments with no Clips

	if (paramManager) {
		ParamCollectionSummary* expressionParamsSummary = paramManager->getExpressionParamSetSummary();
		ExpressionParamSet* expressionParams = (ExpressionParamSet*)expressionParamsSummary->paramCollection;
		if (expressionParams) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithParamCollection* modelStack =
			    setupModelStackWithSong(modelStackMemory, currentSong)
			        ->addTimelineCounter(instrumentPressedForMIDILearn->getActiveClip()) // Could be NULL
			        ->addOtherTwoThingsButNoNoteRow(instrumentPressedForMIDILearn->toModControllable(), paramManager)
			        ->addParamCollection(expressionParams, expressionParamsSummary);

			expressionParams->clearValues(modelStack);
		}
	}
}

void View::ccReceivedForMIDILearn(MIDICable& cable, int32_t channel, int32_t cc, int32_t value) {
	if (thingPressedForMidiLearn != MidiLearn::NONE) {
		deleteMidiCommandOnRelease = false;

		// For MelodicInstruments...
		if (thingPressedForMidiLearn == MidiLearn::INSTRUMENT_INPUT) {

			// Special case for MIDIInstruments - CCs can learn the input MIDI channel
			if (getCurrentOutputType() == OutputType::MIDI_OUT) {

				// But only if user hasn't already started learning MPE stuff... Or regular note-ons...
				if (learnedThing->channelOrZone == MIDI_CHANNEL_NONE) {
					learnedThing->cable = &cable;
					learnedThing->channelOrZone = channel;
					getCurrentInstrument()->beenEdited(false);
				}
			}
		}

		// Or, for all other types of things the user might be holding down...
		else {
			// So long as the value wasn't 0, pretend it was a note-on for command-learn purposes
			if (value) {
				noteOnReceivedForMidiLearn(cable, channel + IS_A_CC, cc, 127);
			}
		}
	}
}

void View::midiLearnFlash() {
	midiLearnFlashOn = !midiLearnFlashOn;
	uiTimerManager.setTimer(TimerName::MIDI_LEARN_FLASH, kFastFlashTime);

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
	// this routine used to exit if shift was held, but the shift+encoder combo does not seem used anywhere else either
	//  if (Buttons::isShiftButtonPressed()) {
	//  	return;
	//  }

	if (activeModControllableModelStack.modControllable) {

		bool noteTailsAllowedBefore;
		ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(whichModEncoder, noteTailsAllowedBefore);

		// If non-existent param, still let the ModControllable know
		if (!modelStackWithParam || !modelStackWithParam->autoParam) {
			modEncoderAction_nonExistentParam(whichModEncoder, offset, modelStackWithParam);
		}

		// Or, if normal case - an actual param
		else {
			modEncoderAction_existentParam(whichModEncoder, offset, modelStackWithParam, noteTailsAllowedBefore);
		}
	}

	instrumentBeenEdited();
}

void View::modEncoderAction_nonExistentParam(int32_t whichModEncoder, int32_t offset,
                                             ModelStackWithAutoParam* modelStackWithParam) {
	ActionResult result = activeModControllableModelStack.modControllable->modEncoderActionForNonExistentParam(
	    offset, whichModEncoder, modelStackWithParam);

	if (result == ActionResult::ACTIONED_AND_CAUSED_CHANGE) {
		setKnobIndicatorLevel(whichModEncoder);
	}
}

void View::modEncoderAction_existentParam(int32_t whichModEncoder, int32_t offset,
                                          ModelStackWithAutoParam* modelStackWithParam, bool noteTailsAllowedBefore) {
	char modelStackTempMemory[MODEL_STACK_MAX_SIZE];
	copyModelStack(modelStackTempMemory, modelStackWithParam, sizeof(ModelStackWithThreeMainThings));
	ModelStackWithThreeMainThings* tempModelStack = (ModelStackWithThreeMainThings*)modelStackTempMemory;

	params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

	int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
	int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
	int32_t lowerLimit;

	if (kind == params::Kind::PATCH_CABLE) {
		lowerLimit = std::min(-192_i32, knobPos);
	}
	else {
		lowerLimit = std::min(-64_i32, knobPos);
	}
	int32_t newKnobPos = knobPos + offset;
	newKnobPos = std::clamp(newKnobPos, lowerLimit, 64_i32);

	// ignore modEncoderTurn for Midi CC if current or new knobPos exceeds 127
	// if current knobPos exceeds 127, e.g. it's 128, then it needs to drop to 126 before a value change
	// gets recorded if newKnobPos exceeds 127, then it means current knobPos was 127 and it was increased
	// to 128. In which case, ignore value change
	if (kind == params::Kind::MIDI && (newKnobPos == 64)) {
		return;
	}

	// if you had selected a parameter in performance view and the parameter name
	// and current value is displayed on the screen, don't show pop-up as the display
	// already shows it
	// this checks that the param displayed on the screen in performance view
	// is the same param currently being edited with mod encoder
	bool editingParamInPerformanceView = false;
	if (getRootUI() == &performanceView) {
		editingParamInPerformanceView =
		    performanceView.possiblyRefreshPerformanceViewDisplay(kind, modelStackWithParam->paramId, newKnobPos);
	}

	// let's see if we're editing the same param in the menu, if so, don't show pop-up
	bool editingParamInMenu = false;

	if (getCurrentUI() == &soundEditor) {
		using namespace deluge::gui::menu_item;

		auto currentItem = soundEditor.getCurrentMenuItem();
		bool inHorizontalMenu =
		    currentItem->isSubmenu()
		    && static_cast<Submenu*>(currentItem)->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL;

		if (!inHorizontalMenu && currentItem->getParamKind() == kind
		    && currentItem->getParamIndex() == modelStackWithParam->paramId) {
			editingParamInMenu = true;
		}
	}

	// let's see if we're browsing for a song
	bool inSongBrowser = getCurrentUI() == &loadSongUI;

	if (!editingParamInPerformanceView && !editingParamInMenu && !inSongBrowser) {
		PatchSource source1 = PatchSource::NONE;
		PatchSource source2 = PatchSource::NONE;
		if (kind == params::Kind::PATCH_CABLE) {
			ParamDescriptor paramDescriptor;
			paramDescriptor.data = modelStackWithParam->paramId;
			source1 = paramDescriptor.getBottomLevelSource();
			if (!paramDescriptor.hasJustOneSource()) {
				source2 = paramDescriptor.getTopLevelSource();
			}
		}
		displayModEncoderValuePopup(kind, modelStackWithParam->paramId, newKnobPos, source1, source2);
	}

	if (newKnobPos == knobPos) {
		return;
	}

	// midi follow and midi feedback enabled
	// re-send midi cc because learned parameter value has changed
	sendMidiFollowFeedback(modelStackWithParam, newKnobPos);

	char newModelStackMemory[MODEL_STACK_MAX_SIZE];

	// Hack to make it so stutter can't be automated
	if (modelStackWithParam->timelineCounterIsSet()
	    && !modelStackWithParam->paramCollection->doesParamIdAllowAutomation(modelStackWithParam)) {
		copyModelStack(newModelStackMemory, modelStackWithParam, sizeof(ModelStackWithAutoParam));
		modelStackWithParam = (ModelStackWithAutoParam*)newModelStackMemory;
		modelStackWithParam->setTimelineCounter(nullptr);
	}

	int32_t newValue = modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

	// Perform the actual change
	modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos, modLength);

	if (activeModControllableModelStack.timelineCounterIsSet()) {
		bool noteTailsAllowedAfter =
		    modelStackWithParam->modControllable->allowNoteTails(tempModelStack->addSoundFlags());

		if (noteTailsAllowedBefore != noteTailsAllowedAfter) {
			if (getRootUI() && getRootUI()->toTimelineView() != nullptr) {
				uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
			}
		}
	}

	// if the newKnobPos == 0, and we're dealing with a param that param that should
	// indicate (blink) middle value
	// then blink that middle value and make it harder to turn the knob past middle
	potentiallyMakeItHarderToTurnKnob(whichModEncoder, modelStackWithParam, newKnobPos);

	// if you're updating a param's value while in the sound editor menu
	// and it's the same param displayed in the automation editor open underneath
	// then refresh the automation editor grid
	if ((getCurrentUI() == &soundEditor) && (getRootUI() == &automationView)) {
		automationView.possiblyRefreshAutomationEditorGrid(getCurrentClip(), kind, modelStackWithParam->paramId);
	}
}

// get's modelStackWithParam for use with Gold Knobs and ModEncoderAction above
// also used with ModButtonAction to display name's of parameters assigned to gold knobs
ModelStackWithAutoParam* View::getModelStackWithParam(int32_t whichModEncoder, bool& noteTailsAllowedBefore) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (activeModControllableModelStack.modControllable) {

		/*
		char newModelStackMemory[MODEL_STACK_MAX_SIZE];
		copyModelStack(newModelStackMemory, &activeModControllableModelStack, sizeof(ModelStackWithThreeMainThings));
		ModelStackWithThreeMainThings* localModelStack = (ModelStackWithThreeMainThings*)newModelStackMemory;
		*/

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

		modelStackWithParam = activeModControllableModelStack.modControllable->getParamFromModEncoder(
		    whichModEncoder, &activeModControllableModelStack);
	}

	return modelStackWithParam;
}

// used to get name of parameter assigned to mod encoder
void View::getParameterNameFromModEncoder(int32_t whichModEncoder, char* parameterName) {
	bool noteTailsAllowedBefore;
	ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(whichModEncoder, noteTailsAllowedBefore);
	if (modelStackWithParam && modelStackWithParam->autoParam) {
		params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

		if (kind == params::Kind::PATCH_CABLE) {
			PatchSource source1 = PatchSource::NONE;
			PatchSource source2 = PatchSource::NONE;
			ParamDescriptor paramDescriptor;
			paramDescriptor.data = modelStackWithParam->paramId;
			source1 = paramDescriptor.getBottomLevelSource();
			if (!paramDescriptor.hasJustOneSource()) {
				source2 = paramDescriptor.getTopLevelSource();
			}

			DEF_STACK_STRING_BUF(paramDisplayName, 30);
			if (source2 == PatchSource::NONE) {
				paramDisplayName.append(getSourceDisplayNameForOLED(source1));
			}
			else {
				paramDisplayName.append(sourceToStringShort(source1));
			}
			if (display->haveOLED()) {
				paramDisplayName.append(" -> ");
			}
			else {
				paramDisplayName.append(" - ");
			}

			if (source2 != PatchSource::NONE) {
				paramDisplayName.append(sourceToStringShort(source2));
				if (display->haveOLED()) {
					paramDisplayName.append(" -> ");
				}
				else {
					paramDisplayName.append(" - ");
				}
			}

			paramDisplayName.append(modulation::params::getPatchedParamShortName(modelStackWithParam->paramId));
			strncpy(parameterName, paramDisplayName.c_str(), 29);
		}
		else {
			strncpy(parameterName, getParamDisplayName(kind, modelStackWithParam->paramId), 29);
		}
	}
}

// for param's that are bipolar / should indicate middle value
// this will blink the middle value when middle knob pos is reached
// and make it harder to turn the knob past the middle value
void View::potentiallyMakeItHarderToTurnKnob(int32_t whichModEncoder, ModelStackWithAutoParam* modelStackWithParam,
                                             int32_t newKnobPos) {
	params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

	// if you're dealing with a patch cable which has a -128 to +128 range
	// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
	if (kind == params::Kind::PATCH_CABLE) {
		newKnobPos = view.convertPatchCableKnobPosToIndicatorLevel(newKnobPos + kKnobPosOffset) - kKnobPosOffset;
	}

	bool shouldParamIndicateMiddleValue =
	    modelStackWithParam->paramCollection->shouldParamIndicateMiddleValue(modelStackWithParam);

	if (newKnobPos == 0 && shouldParamIndicateMiddleValue) {
		bool isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);

		indicator_leds::blinkKnobIndicator(whichModEncoder, isBipolar);

		// Make it harder to turn that knob away from its centred position
		deluge::hid::encoders::timeModEncoderLastTurned[whichModEncoder] = AudioEngine::audioSampleTimer - kSampleRate;
	}
	else {
		indicator_leds::stopBlinkingKnobIndicator(whichModEncoder);
	}
}

void View::displayModEncoderValuePopup(params::Kind kind, int32_t paramID, int32_t newKnobPos, PatchSource source1,
                                       PatchSource source2) {

	// Cache last displayed values to avoid unnecessary notifications
	static params::Kind last_param_kind = params::Kind::NONE;
	static int32_t last_param_id = -1;
	static int32_t last_display_value = INT32_MIN;
	static PatchSource last_source1 = PatchSource::NONE;
	static PatchSource last_source2 = PatchSource::NONE;

	// Display arbitration for multiple mod encoders ("juggling ball" system)
	static params::Kind display_owner_kind = params::Kind::NONE;
	static int32_t display_owner_param_id = -1;
	static PatchSource display_owner_source1 = PatchSource::NONE;
	static PatchSource display_owner_source2 = PatchSource::NONE;
	static uint32_t display_ownership_start_time = 0;
	static uint32_t last_display_update_time = 0; // Used for timeout detection in arbitration
	static uint32_t last_actual_display_time = 0; // Used for frequency throttling

	// Timing constants for display arbitration (in AudioEngine sample units)

	uint32_t current_time = AudioEngine::audioSampleTimer;

	DEF_STACK_STRING_BUF(parameter_name, 40);
	DEF_STACK_STRING_BUF(parameter_value, 40);

	// On OLED, display the name of the parameter on the first line of the popup
	if (display->haveOLED()) {
		if (kind == params::Kind::PATCH_CABLE) {
			parameter_name.append(sourceToStringShort(source1));
			parameter_name.append("->");
			if (source2 != PatchSource::NONE && source2 != PatchSource::NOT_AVAILABLE) {
				parameter_name.append(sourceToStringShort(source2));
				parameter_name.append("->");
			}
			parameter_name.append(modulation::params::getPatchedParamShortName(paramID));
		}
		else if (getCurrentOutputType() == OutputType::MIDI_OUT) {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();
			if (kind == params::Kind::EXPRESSION) {
				if (paramID == X_PITCH_BEND) {
					parameter_name.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
				}
				else if (paramID == Z_PRESSURE) {
					parameter_name.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
				}
				else if (paramID == Y_SLIDE_TIMBRE) {
					// in mono expression this is mod wheel, and y-axis is not directly controllable
					parameter_name.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
				}
			}
			else if (paramID >= 0 && paramID < kNumRealCCNumbers) {
				std::string_view name = midiInstrument->getNameFromCC(paramID);
				if (!name.empty()) {
					parameter_name.append(name.data());
				}
				else {
					parameter_name.append("CC ");
					parameter_name.appendInt(paramID);
				}
			}
		}
		else {
			const char* name = getParamDisplayName(kind, paramID);
			if (name != l10n::get(l10n::String::STRING_FOR_NONE)) {
				parameter_name.append(name);
			}
		}
	}

	// if turning stutter mod encoder and stutter quantize is enabled
	// display stutter quantization instead of knob position
	// int32_t quantization_level = 0;
	int32_t current_display_value = 0;
	if (isParamQuantizedStutter(kind, paramID,
	                            (ModControllableAudio*)view.activeModControllableModelStack.modControllable)
	    && !isUIModeActive(UI_MODE_STUTTERING)) {
		if (newKnobPos < -39) { // 4ths stutter: no leds turned on
			current_display_value = 4;
			parameter_value.append("4ths");
		}
		else if (newKnobPos < -14) { // 8ths stutter: 1 led turned on
			current_display_value = 8;
			parameter_value.append("8ths");
		}
		else if (newKnobPos < 14) { // 16ths stutter: 2 leds turned on
			current_display_value = 16;
			parameter_value.append("16ths");
		}
		else if (newKnobPos < 39) { // 32nds stutter: 3 leds turned on
			current_display_value = 32;
			parameter_value.append("32nds");
		}
		else { // 64ths stutter: all 4 leds turned on
			current_display_value = 64;
			parameter_value.append("64ths");
		}
	}
	// if turning arpeggiator rhythm mod encoder
	else if (isParamArpRhythm(kind, paramID)) {
		current_display_value = calculateKnobPosForDisplay(kind, paramID, newKnobPos + kKnobPosOffset);
		if (display->haveOLED()) {
			char name[12];
			// Index: Name
			snprintf(name, sizeof(name), "%d: %s", current_display_value, arpRhythmPatternNames[current_display_value]);
			parameter_value.append(name);
		}
		else {
			parameter_value.append(arpRhythmPatternNames[current_display_value]);
		}
	}
	else {
		current_display_value = calculateKnobPosForDisplay(kind, paramID, newKnobPos + kKnobPosOffset);
		parameter_value.appendInt(current_display_value);
	}

	// Check if we need to update the notification (avoid excessive updates)
	if (display->haveOLED()) {

		// Check if notification popup is active and if the parameter info has changed
		bool has_param_info_changed = true;
		bool has_min_time_elapsed = true;
		if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
			has_param_info_changed =
			    (kind != last_param_kind || paramID != last_param_id || current_display_value != last_display_value
			     || source1 != last_source1 || source2 != last_source2);

			// Check if enough time has passed since the last actual display update so we
			// can still perceive the changes and so we don't exceed the screen's refresh rate.
			uint32_t time_since_last_actual_display = current_time - last_actual_display_time;
			has_min_time_elapsed = (time_since_last_actual_display >= MIN_UPDATE_INTERVAL);
		}

		// Display arbitration: check if this parameter currently owns the display
		bool current_param_owns_display = (kind == display_owner_kind && paramID == display_owner_param_id
		                                   && source1 == display_owner_source1 && source2 == display_owner_source2);

		// Determine if this parameter can take control of the display
		bool can_take_display_ownership = false;

		if (!display->hasPopupOfType(PopupType::NOTIFICATION)) {
			// No notification currently shown, so anything can take it
			can_take_display_ownership = true;
		}
		else if (current_param_owns_display) {
			// This parameter already owns the display
			can_take_display_ownership = true;
			last_display_update_time = current_time;
		}
		else {
			// Different parameter wants to display - check arbitration rules
			uint32_t time_since_ownership_start = current_time - display_ownership_start_time;
			uint32_t time_since_last_update = current_time - last_display_update_time;

			if (time_since_ownership_start >= MIN_DISPLAY_OWNERSHIP_TIME || time_since_last_update >= DISPLAY_TIMEOUT) {
				// Current owner has had enough time juggling or has stopped updating, so pass it on
				can_take_display_ownership = true;
			}
			// else: it's still the current owner's turn to juggle the ball, so keep it
		}

		// Only update notification if parameter info has changed AND we can take display ownership AND enough time has
		// elapsed
		if (has_param_info_changed && can_take_display_ownership && has_min_time_elapsed) {
			display->displayNotification(parameter_name.c_str(), parameter_value.c_str());

			// Update cached values
			last_param_kind = kind;
			last_param_id = paramID;
			last_display_value = current_display_value;
			last_source1 = source1;
			last_source2 = source2;

			// Update display ownership tracking
			if (!current_param_owns_display) {
				// New parameter taking ownership
				display_owner_kind = kind;
				display_owner_param_id = paramID;
				display_owner_source1 = source1;
				display_owner_source2 = source2;
				display_ownership_start_time = current_time;
			}
			last_display_update_time = current_time;
			last_actual_display_time = current_time; // Track when we actually updated the display
		}
		// Even if no display update needed, refresh timer if same parameter is being adjusted
		else if (current_param_owns_display && display->hasPopupOfType(PopupType::NOTIFICATION)) {
			uiTimerManager.setTimer(TimerName::DISPLAY, 1000);
			last_display_update_time = current_time;
		}
	}
	else {
		display->displayPopup(parameter_value.c_str());
	}
}

// convert deluge internal knobPos range to same range as used by menu's.
int32_t View::calculateKnobPosForDisplay(params::Kind kind, int32_t paramID, int32_t knobPos) {
	if (kind == params::Kind::MIDI) {
		return knobPos;
	}

	float knobPosFloat = static_cast<float>(knobPos);
	float knobPosOffsetFloat = static_cast<float>(kKnobPosOffset);
	float maxKnobPosFloat = static_cast<float>(kMaxKnobPos);
	float maxMenuValueFloat = static_cast<float>(kMaxMenuValue);
	float maxMenuRelativeValueFloat = static_cast<float>(kMaxMenuRelativeValue);
	float valueForDisplayFloat;

	// calculate parameter value for display by converting 0 - 128 range to same range as menu (0 - 50)
	valueForDisplayFloat = (knobPosFloat / maxKnobPosFloat) * maxMenuValueFloat;

	// check if parameter is pan or pitch, in which case, further adjust range from 0 - 50 to -25 to +25
	if (isParamPan(kind, paramID) || isParamPitch(kind, paramID)) {
		valueForDisplayFloat = valueForDisplayFloat - maxMenuRelativeValueFloat;
	}

returnValue:
	return static_cast<int32_t>(std::round(valueForDisplayFloat));
}

void View::instrumentBeenEdited() {
	if (activeModControllableModelStack.timelineCounterIsSet()) {
		activeModControllableModelStack.getTimelineCounter()->instrumentBeenEdited();
	}
}

void View::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {

	// If the learn button is pressed, user is trying to copy or paste, and the fact that we've ended up here means they
	// can't
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
			modEncoderButtonAction_deleteAutomation(whichModEncoder);
		}
		else {
			modEncoderButtonAction_changeModControllable(whichModEncoder, on);
		}
	}
}

void View::modEncoderButtonAction_deleteAutomation(uint8_t whichModEncoder) {
	ModelStackWithAutoParam* modelStackWithParam =
	    activeModControllableModelStack.modControllable->getParamFromModEncoder(whichModEncoder,
	                                                                            &activeModControllableModelStack);

	if (modelStackWithParam && modelStackWithParam->autoParam) {
		Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
		modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam);
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED));
	}
}

void View::modEncoderButtonAction_changeModControllable(uint8_t whichModEncoder, bool on) {
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

void View::setKnobIndicatorLevels() {
	if (!getRootUI()) {
		return; // What's this?
	}

	// don't update knob indicator levels when you're in automation editor
	if ((getRootUI() == &automationView) && automationView.inAutomationEditor()) {
		automationView.displayAutomation();
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
	bool isBipolar = false;

	if (modelStackWithParam->autoParam) {
		int32_t value = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
		ParamCollection* paramCollection = modelStackWithParam->paramCollection;
		params::Kind kind = paramCollection->getParamKind();
		isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);
		knobPos = paramCollection->paramValueToKnobPos(value, modelStackWithParam);
		int32_t lowerLimit;

		if (kind == params::Kind::PATCH_CABLE) {
			lowerLimit = std::min(-192_i32, knobPos);
		}
		else {
			lowerLimit = std::min(-64_i32, knobPos);
		}
		knobPos = std::clamp(knobPos, lowerLimit, 64_i32);
		if (isParamQuantizedStutter(kind, modelStackWithParam->paramId,
		                            (ModControllableAudio*)modelStackWithParam->modControllable)
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
		knobPos += kKnobPosOffset;

		if (kind == params::Kind::PATCH_CABLE) {
			knobPos = view.convertPatchCableKnobPosToIndicatorLevel(knobPos);
		}
	}
	else {
		if (modelStackWithParam->paramId == 255) {
			knobPos = modelStackWithParam->modControllable->getKnobPosForNonExistentParam(whichModEncoder,
			                                                                              modelStackWithParam);
			knobPos += kKnobPosOffset;
		}
		// is it not just a param? then its a patch cable
		else if (!((modelStackWithParam->paramId & 0x0000FF00) == 0x0000FF00)) {
			// default value for patch cable
			// (equals 0 (midpoint) in -128 to +128 range)
			knobPos = 64;
			isBipolar = true;
		}
	}

	indicator_leds::setKnobIndicatorLevel(whichModEncoder, knobPos, isBipolar);
}

/// if you're dealing with a patch cable which has a -128 to +128 range
/// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
int32_t View::convertPatchCableKnobPosToIndicatorLevel(int32_t knobPos) {
	int32_t newKnobPos = (knobPos + kMaxKnobPos) >> 1;
	// adjustment to make sure that when knobPos returned is 64, it's really 64
	// the knob LED indicator is centred around 64
	// so the knob pos returned from this function is used to blink the LED when it reaches 64
	// so to make sure it doesn't blink twice (e.g. when the value is 64 and in between 64 and 65)
	// we adjust it here so it only returns 64 once
	if (newKnobPos == 64 && knobPos != 0) {
		newKnobPos += knobPos;
	}

	return newKnobPos;
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
	RootUI* rootUI = getRootUI();

	// ignore modButtonAction when in the Automation View Automation Editor
	if ((rootUI == &automationView) && automationView.inAutomationEditor()) {
		// exception for arranger view and pressing mod button 0 so you can toggle VU meter
		if (!(automationView.onArrangerView && whichButton == 0)) {
			return;
		}
	}

	pretendModKnobsUntouchedForAWhile();

	if (activeModControllableModelStack.modControllable) {
		if (on) {
			if (isUIModeWithinRange(modButtonUIModes) || (rootUI == &performanceView)) {
				// only displaying VU meter in session view, arranger view, performance view and arranger automation
				// view
				if (!rootUIIsClipMinderScreen()) {
					// are we pressing the same button that is currently selected
					if (*activeModControllableModelStack.modControllable->getModKnobMode() == whichButton) {
						// you just pressed the volume mod button and it was already selected previously
						// toggle displaying VU Meter on / off
						if (whichButton == 0) {
							displayVUMeter = !displayVUMeter;
						}
					}
					// refresh sidebar if VU meter previously rendered is still showing
					if (renderedVUMeter) {
						uiNeedsRendering(rootUI, 0); // only render sidebar
					}
				}

				// change the button selection before calling mod button action so that mod button action
				// knows the mod button parameter context
				*activeModControllableModelStack.modControllable->getModKnobMode() = whichButton;

				activeModControllableModelStack.modControllable->modButtonAction(
				    whichButton, true, (ParamManagerForTimeline*)activeModControllableModelStack.paramManager);

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

	RootUI* rootUI = getRootUI();
	UIType uiType = UIType::NONE;
	UIType uiContextType = UIType::NONE;
	UIModControllableContext uiModControllableContext = UIModControllableContext::NONE;
	if (rootUI) {
		uiType = rootUI->getUIType();
		uiContextType = rootUI->getUIContextType();
		uiModControllableContext = rootUI->getUIModControllableContext();
	}

	// here we will set a boolean flag to let the function know whether we are dealing with the Song context
	bool itsTheSong = ((activeModControllableModelStack.getTimelineCounterAllowNull() == currentSong)
	                   || (uiModControllableContext == UIModControllableContext::SONG));

	// here we will set a boolean flag to let the function know if affect entire is enabled
	// so that it can correctly illuminate the affect entire LED indicator
	bool affectEntire = rootUI && rootUI->getAffectEntire();

	// if you are not in a song
	// affect entire is always true if you are in an audio clip, or automation view for an audio clip
	// otherwise the affect entire status is derived from the instrument clip
	if (!itsTheSong) {
		Clip* clip = getCurrentClip();
		// if you're in an instrument clip, get affectEntire status from clip class
		// otherwise you're in an audio clip or automation view for an audio clip, in which case affect entire is always
		// enabled
		affectEntire = (uiContextType == UIType::INSTRUMENT_CLIP) ? ((InstrumentClip*)clip)->affectEntire : true;
	}
	indicator_leds::setLedState(IndicatorLED::AFFECT_ENTIRE, affectEntire);

	bool onAutomationClipView = false;

	// turn off Clip LED indicator if we're in a song UI
	if (itsTheSong) {
		indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
	}
	// we're in a clip or we've selected a clip
	// here we're going to see if we should blink the CLIP LED if we're in automation view
	// or simply illuminate the CLIP LED if we're not in automation view
	else {
		switch (uiType) {
		case UIType::SESSION: {
			Clip* clip = sessionView.getClipForLayout();

			if (clip) {
				if (clip->onAutomationClipView) {
					onAutomationClipView = true;
				}
			}
			break;
		}
		case UIType::ARRANGER: {
			Output* output = arrangerView.outputsOnScreen[arrangerView.yPressedEffective];

			if (output) {
				if (currentSong->getClipWithOutput(output)->onAutomationClipView) {
					onAutomationClipView = true;
				}
			}
			break;
		}
		case UIType::KEYBOARD_SCREEN:
			if (getCurrentClip()->onAutomationClipView) {
				onAutomationClipView = true;
			}
			break;
		case UIType::AUTOMATION:
			onAutomationClipView = true;
			break;
		default:
		    // fallthrough for everything else -- to many UIs to list explicitly
		    ;
		}

		if (onAutomationClipView) {
			indicator_leds::blinkLed(IndicatorLED::CLIP_VIEW);
		}
		else {
			indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, true);
		}
	}

	// Sort out the session/arranger view/automation arranger view LEDs
	if (itsTheSong) {
		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
		}
		else {
			switch (uiContextType) {
			case UIType::ARRANGER:
				indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW);
				break;
			case UIType::SESSION:
				indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, true);
				break;
			default:
			    // fallthrough for everything else -- to many UIs to list explicitly
			    ;
			}
		}
	}
	// if you're not in the song, you're in a clip, so turn off song LED
	else {
		indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
	}

	// Sort out actual "mod" LEDs
	int32_t modKnobMode = getModKnobMode();

	for (int32_t i = 0; i < kNumModButtons; i++) {
		bool on = (i == modKnobMode);
		// if you're in a song view and volume mod button is selected and VU meter is enabled
		// blink volume mod led
		if (itsTheSong && on && modKnobMode == 0 && view.displayVUMeter) {
			indicator_leds::blinkLed(indicator_leds::modLed[i]);
		}
		// if you're in the Automation View Automation Editor, turn off Mod LED's
		else if ((getRootUI() == &automationView) && automationView.inAutomationEditor()) {
			indicator_leds::setLedState(indicator_leds::modLed[i], false);
		}
		// otherwise update mod led's to reflect current mod led selection
		else {
			indicator_leds::setLedState(indicator_leds::modLed[i], on);
		}
	}
}

int32_t View::getModKnobMode() {
	int32_t modKnobMode = -1;
	if (activeModControllableModelStack.modControllable) {
		uint8_t* modKnobModePointer = activeModControllableModelStack.modControllable->getModKnobMode();
		if (modKnobModePointer) {
			modKnobMode = *modKnobModePointer;
		}
	}
	return modKnobMode;
}

void View::notifyParamAutomationOccurred(ParamManager* paramManager, bool updateModLevels) {
	if (paramManager == activeModControllableModelStack.paramManager
	    || (getCurrentUI() == &soundEditor && paramManager == soundEditor.currentParamManager)) {

		// If timer wasn't set yet, set it now
		if (!uiTimerManager.isTimerSet(TimerName::DISPLAY_AUTOMATION)) {
			pendingParamAutomationUpdatesModLevels = updateModLevels;
			uiTimerManager.setTimer(TimerName::DISPLAY_AUTOMATION, 25);
		}

		else {
			if (updateModLevels) {
				pendingParamAutomationUpdatesModLevels = true;
			}
		}

		if (!uiTimerManager.isTimerSet(TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION)) {
			uiTimerManager.setTimer(TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION, 25);
		}
	}
}

void View::sendMidiFollowFeedback(ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos, bool isAutomation) {
	if (midiEngine.midiFollowFeedbackChannelType != MIDIFollowChannelType::NONE) {
		int32_t channel =
		    midiEngine.midiFollowChannelType[util::to_underlying(midiEngine.midiFollowFeedbackChannelType)]
		        .channelOrZone;
		if (channel != MIDI_CHANNEL_NONE) {
			// check if we're dealing with a clip context param (don't send feedback for song params)
			if (isClipContext()) {
				if (modelStackWithParam && modelStackWithParam->autoParam) {
					params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
					int32_t ccNumber = midiFollow.getCCFromParam(kind, modelStackWithParam->paramId);
					if (ccNumber != MIDI_CC_NONE) {
						midiFollow.sendCCForMidiFollowFeedback(channel, ccNumber, knobPos);
					}
				}
				else {
					midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(channel, isAutomation);
				}
			}
		}
	}
}

// sets flag to let caller know if we are dealing with clip context
bool View::isClipContext() {
	bool itsAClip = false;
	if (activeModControllableModelStack.modControllable) {
		itsAClip = (activeModControllableModelStack.timelineCounterIsSet()
		            && activeModControllableModelStack.getTimelineCounter() != currentSong);
	}
	return itsAClip;
}

void View::displayAutomation() {
	if (pendingParamAutomationUpdatesModLevels) {
		setKnobIndicatorLevels();
	}
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->readValueAgain();
	}
}

/// if you've toggled showing the VU meter, and the mod encoders are controllable (e.g. affect entire on)
/// and the current mod button selected is the volume/pan button
/// render VU meter on the grid
bool View::potentiallyRenderVUMeter(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// if VU meter is toggled on and
	// 1) mod controllable is active and we've selected the level / pan mod knob button
	// or
	// 2) we've pressed a clip while vu meter was active
	// then let's continue to render VU meter
	if (displayVUMeter
	    && ((activeModControllableModelStack.modControllable
	         && *activeModControllableModelStack.modControllable->getModKnobMode() == 0)
	        || (isClipContext() && renderedVUMeter))) {
		PadLEDs::renderingLock = true;

		// get max Y display that would be rendered based on AudioEngine::approxRMSLevel
		int32_t maxYDisplayForVUMeterL = getMaxYDisplayForVUMeter(AudioEngine::approxRMSLevel.l);
		int32_t maxYDisplayForVUMeterR = getMaxYDisplayForVUMeter(AudioEngine::approxRMSLevel.r);

		// if we haven't yet rendered
		// or previously rendered VU meter was rendered to a different maxYDisplay
		// then we want to refresh the VU meter rendered in the sidebar
		// if we've already rendered and maxYDisplay hasn't changed, no need to refresh sidebar image
		if (!renderedVUMeter || (maxYDisplayForVUMeterL != cachedMaxYDisplayForVUMeterL)
		    || (maxYDisplayForVUMeterR != cachedMaxYDisplayForVUMeterR)) {
			// save maxYDisplay about to be rendered
			cachedMaxYDisplayForVUMeterL = maxYDisplayForVUMeterL;
			cachedMaxYDisplayForVUMeterR = maxYDisplayForVUMeterR;

			// erase current image as it will be refreshed
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				RGB* const start = &image[y][kDisplayWidth];
				std::fill(start, start + kSideBarWidth, colours::black);
			}

			// render left VU meter
			if (maxYDisplayForVUMeterL != 255) {
				renderVUMeter(maxYDisplayForVUMeterL, kDisplayWidth, image);
			}

			// render right VU meter
			if (maxYDisplayForVUMeterR != 255) {
				renderVUMeter(maxYDisplayForVUMeterR, kDisplayWidth + 1, image);
			}
			// save the VU meter rendering status so that grid can be refreshed later if required
			// (e.g. if you switch mod buttons or turn off affect entire)
			renderedVUMeter = true;
		}

		PadLEDs::renderingLock = false;

		// return true so that you don't render the usual sidebar
		return true;
	}

	// if we made it here then we haven't rendered a VU meter in the sidebar
	renderedVUMeter = false;

	// return false so that the usual sidebar rendering can be drawn
	return false;
}

// lookup table for the min value of each pad's value range used to display vu meter on the grid
const float dBFSForYDisplay[kDisplayHeight] = {-30.8, -26.4, -22.0, -17.6, -13.2, -8.8, -4.4, -0.2};

int32_t View::getMaxYDisplayForVUMeter(float level) {
	// dBFS (dB below clipping) calculation
	// 16.7 = log(2^24) which is the approxRMSLevel at which clipping begins
	float dBFS = (level - 16.7) * 4;
	int32_t maxYDisplay = 255;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		// dBFSForYDisplay calculates the minimum value of the dBFS ranged displayed for a given grid row (Y)
		// 9 is the approxRMSLevel at which the sound becomes inaudible
		// so for grid rendering purposes, any approxRMSLevel value below 9 doesn't get rendered on grid
		// -30.8 dBFS = (9 - 16.7) * 4
		// 4.4 = 4.3 is dBFS range for a given row + 0.1
		// 0.1 is added to the dBFS range for a given row to arrive at the minimum value for the next row
		/*
		y7 = clipping (-0.2 or higher)
		y6 = -4.4 to -0.3
		y5 = -8.8 to -4.5
		y4 = -13.2 to -8.9
		y3 = -17.6 to -13.3
		y2 = -22.0 to -17.7
		y1 = -26.4 to -22.1
		y0 = -30.8 to -26.5
		*/
		// if dBFS >= dBFSForYDisplay it means that the dBFS value should be rendered in that Y row
		if (dBFS >= dBFSForYDisplay[yDisplay]) {
			maxYDisplay = yDisplay;
		}
		else {
			break;
		}
	}
	return maxYDisplay;
}

/// render AudioEngine::approxRMSLevel as a VU meter on the grid
void View::renderVUMeter(int32_t maxYDisplay, int32_t xDisplay, RGB thisImage[][kDisplayWidth + kSideBarWidth]) {
	for (int32_t yDisplay = 0; yDisplay < (maxYDisplay + 1); yDisplay++) {
		// y0 - y4 = green
		if (yDisplay < 5) {
			thisImage[yDisplay][xDisplay] = colours::green;
		}
		// y5 - y6 = orange
		else if (yDisplay < 7) {
			thisImage[yDisplay][xDisplay] = colours::orange;
		}
		// y7 = red
		else {
			thisImage[yDisplay][xDisplay] = colours::red;
		}
	}
}

void View::setActiveModControllableTimelineCounter(TimelineCounter* timelineCounter, bool shouldSendMidiFeedback) {
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
		modelStack->addOtherTwoThingsButNoNoteRow(nullptr, nullptr);
	}

	setModLedStates();
	setKnobIndicatorLevels();

	// refresh sidebar if VU meter previously rendered is still showing and we're in session / arranger / performance
	// view / arranger automation view this could happen when you're turning affect entire off or selecting a clip
	if (renderedVUMeter && !rootUIIsClipMinderScreen()) {
		uiNeedsRendering(getRootUI(), 0);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc's because learned parameter values may have changed
	if (shouldSendMidiFeedback) {
		sendMidiFollowFeedback();
	}
}

void View::setActiveModControllableWithoutTimelineCounter(ModControllable* modControllable,
                                                          ParamManager* paramManager) {

	pretendModKnobsUntouchedForAWhile(); // Why again?

	setupModelStackWithSong(&activeModControllableModelStack, currentSong)
	    ->addTimelineCounter(nullptr)
	    ->addOtherTwoThingsButNoNoteRow(modControllable, paramManager);

	setModLedStates();
	setKnobIndicatorLevels();

	// refresh sidebar if VU meter previously rendered is still showing and we're in session / arranger / performance
	// view / arranger automation view this could happen when you're turning affect entire off or selecting a clip
	if (renderedVUMeter && !rootUIIsClipMinderScreen()) {
		uiNeedsRendering(getRootUI(), 0);
	}

	// midi follow and midi feedback enabled
	// re-send midi cc's because learned parameter values may have changed
	sendMidiFollowFeedback();
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

	// midi follow and midi feedback enabled
	// re-send midi cc's because learned parameter values may have changed
	// don't send if midi follow feedback automation is disabled
	if (midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED) {
		sendMidiFollowFeedback(nullptr, kNoSelection, true);
	}
}

void View::pretendModKnobsUntouchedForAWhile() {
	encoders::timeModEncoderLastTurned[0] = encoders::timeModEncoderLastTurned[1] =
	    AudioEngine::audioSampleTimer - kSampleRate;
}

void View::cycleThroughReverbPresets() {

	int32_t currentPreset = getCurrentReverbPreset();

	int32_t newPreset = currentPreset + 1;
	if (newPreset >= NUM_PRESET_REVERBS) {
		newPreset = 0;
	}

	AudioEngine::reverb.setRoomSize((float)presetReverbRoomSize[newPreset] / 50);
	AudioEngine::reverb.setDamping((float)presetReverbDamping[newPreset] / 50);
}

int32_t View::getCurrentReverbPreset() {
	int32_t currentRoomSize = AudioEngine::reverb.getRoomSize() * 50;
	int32_t currentDamping = AudioEngine::reverb.getDamping() * 50;

	// See which preset we're the closest to currently
	int32_t lowestDifferentness = 1000;
	int32_t currentPreset;
	for (int32_t p = 0; p < NUM_PRESET_REVERBS; p++) {
		int32_t differentness =
		    std::abs(currentRoomSize - presetReverbRoomSize[p]) + std::abs(currentDamping - presetReverbDamping[p]);
		if (differentness < lowestDifferentness) {
			lowestDifferentness = differentness;
			currentPreset = p;
		}
	}

	return currentPreset;
}

char const* View::getReverbPresetDisplayName(int32_t preset) {
	return deluge::l10n::get(presetReverbNames[preset]);
}

void View::displayOutputName(Output* output, bool doBlink, Clip* clip) {
	int32_t channel{0}, channelSuffix{0};
	bool editedByUser = true;
	if (output->type != OutputType::AUDIO) {
		Instrument* instrument = (Instrument*)output;
		editedByUser = !instrument->existsOnCard;
		switch (output->type) {
		case OutputType::MIDI_OUT:
			channelSuffix = ((MIDIInstrument*)instrument)->channelSuffix;
			// No break

		case OutputType::CV:
			channel = ((NonAudioInstrument*)instrument)->getChannel();
			break;

		// explicit fallthrough cases
		case OutputType::SYNTH:
		case OutputType::KIT:
		case OutputType::AUDIO:
		case OutputType::NONE:;
		}
	}
	else {
		channel = static_cast<int32_t>(((AudioOutput*)output)->mode);
	}

	drawOutputNameFromDetails(output->type, channel, channelSuffix, output->name.get(), output->name.isEmpty(),
	                          editedByUser, doBlink, clip);
	deluge::hid::display::OLED::markChanged();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
void View::drawOutputNameFromDetails(OutputType outputType, int32_t channel, int32_t channelSuffix, char const* name,
                                     bool isNameEmpty, bool editedByUser, bool doBlink, Clip* clip) {
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

		bool isGridView =
		    (getCurrentUI() == &sessionView && currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid);

		if (outputType != OutputType::AUDIO) {
			blinkLed(led);
		}

		InstrumentClip* clip = nullptr;
		if (clip && clip->type == ClipType::INSTRUMENT) {
			clip = (InstrumentClip*)clip;
		}

		setLedState(LED::KEYBOARD, (clip && clip->onKeyboardScreen));
		setLedState(LED::SCALE_MODE, (clip && clip->inScaleMode && clip->output->type != OutputType::KIT));
		setLedState(LED::CROSS_SCREEN_EDIT, (clip && clip->wrapEditing));
	}

	// hook to render display for OLED and 7SEG when in Automation View
	if (getCurrentUI() == &automationView && !isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		if (automationView.inAutomationEditor()) {
			automationView.displayAutomation(true, !display->have7SEG());
		}
		else {
			automationView.renderDisplay();
		}
		return;
	}

	if (display->haveOLED()) {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
		hid::display::OLED::clearMainImage();

		char const* outputTypeText = getOutputTypeName(outputType, channel);

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
		canvas.drawStringCentred(outputTypeText, yPos, kTextSpacingX, kTextSpacingY);
	}

	char buffer[12];
	char const* nameToDraw = nullptr;

	if (!isNameEmpty) {
		if (display->haveOLED()) {
			nameToDraw = name;
oledDrawString:
			deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
#if OLED_MAIN_HEIGHT_PIXELS == 64
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 30;
#else
			int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 17;
#endif

			int32_t stringLengthPixels = canvas.getStringWidthInPixels(nameToDraw, kTextTitleSizeY);

			if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
				canvas.drawStringCentred(nameToDraw, yPos, kTextTitleSpacingX, kTextTitleSizeY);
			}
			else {
				canvas.drawString(nameToDraw, 0, yPos, kTextTitleSpacingX, kTextTitleSizeY);
				deluge::hid::display::OLED::setupSideScroller(0, nameToDraw, 0, OLED_MAIN_WIDTH_PIXELS, yPos,
				                                              yPos + kTextTitleSizeY, kTextTitleSpacingX,
				                                              kTextTitleSizeY, false);
			}

			if (clip) {
				// "SECTION NN" is 10, "NN: " is 3 => 10 over current name is always enough.
				DEF_STACK_STRING_BUF(info, clip->name.getLength() + 10);
				if (clip->name.isEmpty()) {
					info.append("Section ");
					info.appendInt(clip->section + 1);
				}
				else {
					info.appendInt(clip->section + 1);
					info.append(": ");
					info.append(clip->name.get());
				}
				yPos = yPos + 14;
				canvas.drawStringCentred(info.data(), yPos, kTextSpacingX, kTextSpacingY);
				deluge::hid::display::OLED::setupSideScroller(1, info.data(), 0, OLED_MAIN_WIDTH_PIXELS, yPos,
				                                              yPos + kTextSpacingY, kTextSpacingX, kTextSpacingY,
				                                              false);
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
			else if (channel == MIDI_CHANNEL_MPE_LOWER_ZONE || channel == MIDI_CHANNEL_MPE_UPPER_ZONE) {
				nameToDraw = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
				goto oledDrawString;
			}
			else {
				nameToDraw = "Transpose";
				goto oledDrawString;
			}
		}
		else {
			if (channel < 16) {
				display->setTextAsSlot(channel + 1, channelSuffix, false, doBlink);
			}
			else if (channel == MIDI_CHANNEL_MPE_LOWER_ZONE || channel == MIDI_CHANNEL_MPE_UPPER_ZONE) {
				char const* text = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper";
				display->setText(text, false, 255, doBlink);
			}
			else {
				display->setText("Transpose", false, 255, doBlink);
			}
		}
	}
	else if (outputType == OutputType::CV) {
		if (display->haveOLED()) {
			if (channel < both) {
				intToString(channel + 1, buffer);
			}
			else {
				sprintf(buffer, "1 and 2");
			}
oledOutputBuffer:
			nameToDraw = buffer;
			goto oledDrawString;
		}
		else {
			if (channel < both) {
				display->setTextAsNumber(channel + 1, 255, doBlink);
			}
			else {
				display->setText("Both");
			}
		}
	}
}
#pragma GCC diagnostic pop

void View::navigateThroughAudioOutputsForAudioClip(int32_t offset, AudioClip* clip, bool doBlink) {

	AudioEngine::logAction("navigateThroughPresets");

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	// Work out availabilityRequirement. But we don't in this case need to think about whether the Output can be
	// "replaced" - that's for InstrumentClips
	Availability availabilityRequirement;
	currentSong->shouldOldOutputBeReplaced(clip, &availabilityRequirement);

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
	bool oldInstrumentCanBeReplaced = modelStack->song->shouldOldOutputBeReplaced(clip, &availabilityRequirement);

	bool shouldReplaceWholeInstrument;

	Instrument* newInstrument;
	Instrument* oldInstrument = (Instrument*)clip->output;

	// If we're in MIDI or CV mode, easy - just change the channel
	if (outputType == OutputType::MIDI_OUT || outputType == OutputType::CV) {

		NonAudioInstrument* oldNonAudioInstrument = (NonAudioInstrument*)oldInstrument;
		int32_t newChannel = oldNonAudioInstrument->getChannel();
		int32_t newChannelSuffix;
		if (outputType == OutputType::MIDI_OUT) {
			newChannelSuffix = ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix;
		}

		// TODO: the contents of these badly wants to be replaced with how I did it in changeOutputType()!

		// CV
		if (outputType == OutputType::CV) {
			while (true) {
				newChannel = CVInstrument::navigateChannels(newChannel, offset);

				if (newChannel == oldNonAudioInstrument->getChannel()) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_UNUSED_CHANNELS));
					return;
				}

				if (availabilityRequirement == Availability::ANY) {
					break;
				}
				else if (availabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
					int channelToSearch = newChannel;
					if (newChannel == CVInstrumentMode::both) {
						// in this case we just need to make sure the one were not about to give up is free
						// there probably should be a gatekeeper managing the cv/gate resources but that's a lot to
						// change and this doesn't matter much
						channelToSearch = oldNonAudioInstrument->getChannel() == 0 ? 1 : 0;
					}
					if (!modelStack->song->doesNonAudioSlotHaveActiveClipInSession(outputType, channelToSearch)) {
						break;
					}
				}
				else if (availabilityRequirement == Availability::INSTRUMENT_UNUSED) {
					if (!modelStack->song->getInstrumentFromPresetSlot(outputType, newChannel, -1, nullptr, nullptr,
					                                                   false)) {
						break;
					}
				}
			}
		}

		// Or MIDI
		else {

			int32_t oldChannel = newChannel;

			if (oldInstrumentCanBeReplaced) {
				oldNonAudioInstrument->setChannel(-1); // Get it out of the way
			}

			while (true) {
				newChannelSuffix += offset;

				// Turned left
				if (offset == -1) {
					if (newChannelSuffix < -1) {
						newChannel = (newChannel + offset);
						if (newChannel < 0) {
							newChannel = IS_A_DEST + NUM_INTERNAL_DESTS;
						}
						else if (newChannel > MIDI_CHANNEL_MPE_UPPER_ZONE && newChannel <= IS_A_DEST) {
							newChannel = MIDI_CHANNEL_MPE_UPPER_ZONE;
						}
						newChannelSuffix = modelStack->song->getMaxMIDIChannelSuffix(newChannel);
					}
				}

				// Turned right
				else {

					if (newChannelSuffix >= 26
					    || newChannelSuffix > modelStack->song->getMaxMIDIChannelSuffix(newChannel)) {
						newChannel = (newChannel + offset);
						if (newChannel > MIDI_CHANNEL_MPE_UPPER_ZONE && newChannel <= IS_A_DEST) {
							newChannel = IS_A_DEST + 1;
						}
						else if (newChannel > IS_A_DEST + NUM_INTERNAL_DESTS) {
							newChannel = 0;
						}
						newChannelSuffix = -1;
					}
				}

				if (newChannel == oldChannel
				    && newChannelSuffix == ((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix) {
					oldNonAudioInstrument->setChannel(oldChannel); // Put it back
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
					if (!modelStack->song->getInstrumentFromPresetSlot(outputType, newChannel, newChannelSuffix,
					                                                   nullptr, nullptr, false)) {
						break;
					}
				}
			}

			oldNonAudioInstrument->setChannel(oldChannel); // Put it back
		}

		newInstrument = modelStack->song->getInstrumentFromPresetSlot(outputType, newChannel, newChannelSuffix, nullptr,
		                                                              nullptr, false);
		// this can happen specifically with cv to handle channels 1+2 together
		if (newInstrument == oldInstrument) {
			newInstrument = nullptr;
		}

		shouldReplaceWholeInstrument = (oldInstrumentCanBeReplaced && !newInstrument);

		// If we want to "replace" the old Instrument, we can instead sneakily just modify its channel
		if (shouldReplaceWholeInstrument) {
			if (playbackHandler.isEitherClockActive()) {
				clip->output->getActiveClip()->expectNoFurtherTicks(modelStack->song);
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			memcpy(modelStackMemory, modelStack, sizeof(ModelStack));

			clip->output->stopAnyAuditioning((ModelStack*)modelStackMemory);

			// Because these are just MIDI / CV instruments and we're changing them for all Clips, we can just change
			// the existing Instrument object!
			oldNonAudioInstrument->setChannel(newChannel);
			if (outputType == OutputType::MIDI_OUT) {
				((MIDIInstrument*)oldNonAudioInstrument)->channelSuffix = newChannelSuffix;
			}

			newInstrument = oldNonAudioInstrument;
		}

		// Otherwise...
		else {

			bool instrumentAlreadyInSong = (newInstrument != nullptr);

			// If an Instrument doesn't yet exist for the new channel we're gonna use...
			if (!newInstrument) {
				if (outputType == OutputType::MIDI_OUT) {
					newInstrument = modelStack->song->grabHibernatingMIDIInstrument(newChannel, newChannelSuffix);
					if (newInstrument) {
						goto gotAnInstrument;
					}
				}
				newInstrument = StorageManager::createNewNonAudioInstrument(outputType, newChannel, newChannelSuffix);
				if (!newInstrument) {
					display->displayError(Error::INSUFFICIENT_RAM);
					return;
				}

				// We just allocated a brand new Instrument in RAM. If MIDI, copy knob assignments from old Instrument
				if (outputType == OutputType::MIDI_OUT) {
					MIDIInstrument* newMIDIInstrument = (MIDIInstrument*)newInstrument;
					MIDIInstrument* oldMIDIInstrument = (MIDIInstrument*)clip->output;
					newMIDIInstrument->modKnobCCAssignments = oldMIDIInstrument->modKnobCCAssignments;
					newInstrument->editedByUser =
					    oldNonAudioInstrument->editedByUser; // This keeps a record of "whether there are any CC
					                                         // assignments", so must be copied across
				}

				// And, we'd better copy the default velocity too
				newInstrument->defaultVelocity = oldNonAudioInstrument->defaultVelocity;
			}
gotAnInstrument:

			Error error = clip->changeInstrument(modelStack, newInstrument, nullptr,
			                                     InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, nullptr, true);
			// TODO: deal with errors

			if (!instrumentAlreadyInSong) {
				modelStack->song->addOutput(newInstrument);
			}
		}

		displayOutputName(newInstrument, doBlink);

		// Special case: when it is a saved MIDI preset (with a name), then we need to show the channel in a popup, as
		// the name will print over the midi channel and we can't see it while changing it
		if (outputType == OutputType::MIDI_OUT && newInstrument->name.getLength() > 0) {
			char buffer[12];
			if (newChannel < 16) {
				slotToString(newChannel + 1, newChannelSuffix, buffer, 1);
			}
			else if (newChannel == MIDI_CHANNEL_MPE_LOWER_ZONE || newChannel == MIDI_CHANNEL_MPE_UPPER_ZONE) {
				strcpy(buffer, (newChannel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "Lower" : "Upper");
			}
			else {
				strcpy(buffer, "Transpose");
			}
			display->popupTextTemporary(buffer);
		}
	}

	// Or if we're on a Kit or Synth...
	else {

		PresetNavigationResult results =
		    loadInstrumentPresetUI.doPresetNavigation(offset, oldInstrument, availabilityRequirement, false);
		if (results.error == Error::NO_ERROR_BUT_GET_OUT) {
getOut:
			display->removeWorkingAnimation();
			return;
		}
		if (results.error != Error::NONE) {
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

			// If we're here, we know the Clip is not playing in the arranger (and doesn't even have an instance in
			// there)

			Error error = clip->changeInstrument(modelStack, newInstrument, nullptr,
			                                     InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, nullptr, true);
			// TODO: deal with errors!

			if (!instrumentAlreadyInSong) {
				modelStack->song->addOutput(newInstrument);
			}
		}

		// Kit-specific stuff
		if (outputType == OutputType::KIT) {
			clip->ensureScrollWithinKitBounds();
			((Kit*)newInstrument)->selectedDrum = nullptr;
		}

		RootUI* rootUI = getRootUI();
		if (rootUI == &instrumentClipView || rootUI == &automationView) {
			// Sean: replace routineWithClusterLoading call, just yield to run a single thing (probably audio)
			yield([]() { return true; });
			instrumentClipView.recalculateColours();

			uiNeedsRendering(rootUI);
		}

		display->removeLoadingAnimation();
	}

	instrumentChanged(modelStack, newInstrument);

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager(
	    "E058",
	    "H058"); // I got this during limited-RAM testing. Maybe there wasn't enough RAM to create the ParamManager or
	             // store its backup?
}

// Returns whether success
bool View::changeOutputType(OutputType newOutputType, ModelStackWithTimelineCounter* modelStack, bool doBlink) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	OutputType oldOutputType = clip->output->type;
	if (oldOutputType == newOutputType) {
		return false;
	}

	// don't allow clip type change if clip is not empty
	// only impose this restriction if switching to/from kit clip
	if (((oldOutputType == OutputType::KIT) || (newOutputType == OutputType::KIT))
	    && (!clip->isEmpty() || !clip->output->isEmpty())) {
		return false;
	}

	Instrument* newInstrument = clip->changeOutputType(modelStack, newOutputType);
	if (!newInstrument) {
		return false;
	}

	// Do a redraw. Obviously the Clip is the same
	setActiveModControllableTimelineCounter(clip);
	displayOutputName(newInstrument, doBlink, clip);

	return true;
}

void View::instrumentChanged(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument) {

	((Clip*)modelStack->getTimelineCounter())->outputChanged(modelStack, newInstrument);
	// Do a redraw. Obviously the Clip is the same
	setActiveModControllableTimelineCounter(modelStack->getTimelineCounter());

	if (newInstrument != nullptr) {
		keyboardScreen.checkNewInstrument(newInstrument);
	}
}

RGB View::getClipMuteSquareColour(Clip* clip, RGB thisColour, bool allowMIDIFlash) {

	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING && clip && clip->armedForRecording) {
		if (blinkOn) {
			bool shouldGoPurple = clip->type == ClipType::AUDIO && ((AudioClip*)clip)->overdubsShouldCloneOutput;

			// Bright colour
			if (clip->wantsToBeginLinearRecording(currentSong)) {
				if (shouldGoPurple) {
					return colours::magenta;
				}
				return colours::red;
			}
			// Dull colour, cos can't actually begin linear recording despite being armed
			if (shouldGoPurple) {
				return colours::magenta_dull;
			}
			return colours::red_dull;
		}
		return colours::black;
	}

	// If user assigning MIDI controls and this Clip has a command assigned, flash pink
	if (allowMIDIFlash && midiLearnFlashOn && clip->muteMIDICommand.containsSomething()) {
		return colours::midi_command;
	}

	if (clipArmFlashOn && clip->armState != ArmState::OFF) {
		thisColour = colours::black;
	}

	// If it's soloed or armed to solo, blue
	else if (clip->soloingInSessionMode || clip->armState == ArmState::ON_TO_SOLO) {
		thisColour = menu_item::soloColourMenu.getRGB();
	}

	// Or if not soloing...
	else {
		if (!clip->activeIfNoSolo) {
			switch (clip->launchStyle) {
			case LaunchStyle::FILL:
				thisColour = menu_item::fillColourMenu.getRGB(); // colours::red_orange;
				break;
			case LaunchStyle::ONCE:
				thisColour = menu_item::onceColourMenu.getRGB(); // colours::red_orange;
				break;
			default:
				// If it's stopped, red.
				thisColour = menu_item::stoppedColourMenu.getRGB();
			}
		}
		else {
			// Active pads of any type go green (or the active colour from the menu).
			thisColour = menu_item::activeColourMenu.getRGB();
		}

		if (currentSong->getAnyClipsSoloing()) {
			thisColour = thisColour.dull();
		}
	}

	// If user assigning MIDI controls and has this Clip selected, flash to half brightness
	if (midiLearnFlashOn && learnedThing == &clip->muteMIDICommand) {
		thisColour = thisColour.dim();
	}
	return thisColour;
}

ActionResult View::clipStatusPadAction(Clip* clip, bool on, int32_t yDisplayIfInSessionView) {

	switch (currentUIMode) {
	case UI_MODE_MIDI_LEARN:
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		view.clipStatusMidiLearnPadPressed(on, clip);
		if (!on) {
			RootUI* rootUI = getRootUI();
			if ((rootUI == &sessionView) || (rootUI == &performanceView)) {
				uiNeedsRendering(rootUI, 0, 1 << yDisplayIfInSessionView);
			}
		}
		break;

	case UI_MODE_VIEWING_RECORD_ARMING:
		if (on) {
			if (!clip->armedForRecording) {
				clip->armedForRecording = true;
				if (clip->type == ClipType::AUDIO) {
					((AudioClip*)clip)->overdubsShouldCloneOutput = false;
					currentSong->defaultAudioClipOverdubOutputCloning = 0;
				}
			}
			else {
				if (clip->type == ClipType::AUDIO && !((AudioClip*)clip)->overdubsShouldCloneOutput) {
					((AudioClip*)clip)->overdubsShouldCloneOutput = true;
					currentSong->defaultAudioClipOverdubOutputCloning = 1;
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
		// If the user was just quick and is actually holding the record button but the submode just hasn't changed
		// yet...
		if (on && Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			clip->armedForRecording = !clip->armedForRecording;
			sessionView.timerCallback(); // Get into UI_MODE_VIEWING_RECORD_ARMING. TODO: this needs doing properly -
			                             // what if we're in a Clip view?
			break;
		}
		// No break
	case UI_MODE_CLIP_PRESSED_IN_SONG_VIEW:

	case UI_MODE_HOLDING_STATUS_PAD:
		if (on) {
			enterUIMode(UI_MODE_HOLDING_STATUS_PAD);
			context_menu::clip_settings::clipSettings.clip = clip;
			sessionView.performActionOnPadRelease = false; // Even though there's a chance we're not in session view
			session.toggleClipStatus(clip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_STATUS_PAD);
		}
		break;

	case UI_MODE_STUTTERING:
		// this code is needed to allow users to launch clips while stuttering
		// without it the deluge becomes unresponsive if you try to launch a clip while stuttering
		// this is because it gets stuck in the stuttering UI mode and can't get out
		if (on) {
			sessionView.performActionOnPadRelease = false; // Even though there's a chance we're not in session view
			session.toggleClipStatus(clip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
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

void View::flashPlayRoutine() {
	view.clipArmFlashOn = !view.clipArmFlashOn;
	RootUI* rootUI = getRootUI();
	if ((rootUI == &sessionView) || (rootUI == &performanceView)) {
		sessionView.flashPlayRoutine();
	}
	else {
		// TODO: sidebar might not actually be visible, flash song button in that case?
		uiNeedsRendering(getCurrentUI(), 0x00000000, 0xFFFFFFFF);
	}
}

void View::flashPlayEnable() {
	uiTimerManager.setTimer(TimerName::PLAY_ENABLE_FLASH, kFastFlashTime);
}

void View::flashPlayDisable() {
	clipArmFlashOn = false;
	uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);

	RootUI* rootUI = getRootUI();
	if ((rootUI == &sessionView) || (rootUI == &performanceView)) {
		uiNeedsRendering(rootUI, 0, 0xFFFFFFFF);
	}
#ifdef currentClipStatusButtonX
	else if (getRootUI()->toClipMinder()) {
		drawCurrentClipPad(getCurrentClip());
	}
#endif
}

bool View::renderMacros(int32_t column, uint32_t y, int32_t selectedMacro, RGB image[][kDisplayWidth + kSideBarWidth],
                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;

	bool is_active = selectedMacro == y;
	bool is_other_active = selectedMacro >= 0 && !is_active;
	uint8_t dark = is_active ? 32 : 0;
	uint8_t light = is_other_active ? 208 : 255;

	bool armed = view.clipArmFlashOn;

	SessionMacro& m = currentSong->sessionMacros[y];
	switch (m.kind) {
	case CLIP_LAUNCH:
		if (m.clip->activeIfNoSolo) {
			image[y][column] = {0, light, 0};
		}
		else {
			image[y][column] = {light, 0, 0};
		}
		if (m.clip->armState != ArmState::OFF) {
			armed = true;
			if (view.clipArmFlashOn) {
				image[y][column] = {0, 0, 0};
			}
		}

		break;
	case OUTPUT_CYCLE:
		image[y][column] = {0, 64, light};
		break;
	case SECTION:
		image[y][column] = {light, 0, 128};
		break;
	case NO_MACRO:
		image[y][column] = {dark, dark, dark};
		break;
	// explicit fallthrough
	case NUM_KINDS:;
	}

	if (occupancyMask) {
		occupancyMask[y][column] = true;
	}

	return armed;
}

void View::activateMacro(uint32_t y) {
	if (y > 8) {
		return;
	}

	SessionMacro& m = currentSong->sessionMacros[y];
	switch (m.kind) {
	case CLIP_LAUNCH:
		if (Buttons::isButtonPressed(deluge::hid::button::AFFECT_ENTIRE)) {
			if (getCurrentClip() != m.clip) {
				sessionView.transitionToViewForClip(m.clip);
			}
		}
		else {
			session.toggleClipStatus(m.clip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
		}
		break;

	case OUTPUT_CYCLE: {
		Clip* nextClip = findNextClipForOutput(m.output);
		if (nextClip) {
			session.toggleClipStatus(nextClip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
		}
		break;
	}

	case SECTION:
		session.armSection(m.section, kInternalButtonPressLatency);
		break;

	default:
		break;
	}
}

Clip* View::findNextClipForOutput(Output* output) {
	int last_active = -1;
	for (int i = 0; i < currentSong->sessionClips.getNumElements(); i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip->output == output) {
			if (last_active == -1) {
				if (clip->activeIfNoSolo) {
					last_active = i;
				}
			}
			else {
				return clip;
			}
		}
	}

	if (last_active == -1) {
		last_active = currentSong->sessionClips.getNumElements();
	}

	// might need to cycle around to find the next clip
	for (int i = 0; i < last_active; i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip->output == output) {
			return clip;
		}
	}

	return nullptr;
}

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = setupModelStack(modelStackMemory);
*/
