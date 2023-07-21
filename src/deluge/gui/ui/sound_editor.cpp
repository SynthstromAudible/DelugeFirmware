
#include "definitions.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "extern.h"
#include "gui/context_menu/overwrite_bootloader.h"
#include "gui/ui_timer_manager.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard_screen.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/debug/print.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "util/comparison.h"
#include "util/functions.h"
#include <new>
#include <string.h>

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

#include "menus.h"

using namespace deluge;
using namespace menu_item;

#define comingSoonMenu (MenuItem*)0xFFFFFFFF

// 255 means none. 254 means soon
uint8_t modSourceShortcuts[2][8] = {
    {255, 255, 255, 255, 255, PATCH_SOURCE_LFO_GLOBAL, PATCH_SOURCE_ENVELOPE_0, PATCH_SOURCE_X},

    {PATCH_SOURCE_AFTERTOUCH, PATCH_SOURCE_VELOCITY, PATCH_SOURCE_RANDOM, PATCH_SOURCE_NOTE, PATCH_SOURCE_COMPRESSOR,
     PATCH_SOURCE_LFO_LOCAL, PATCH_SOURCE_ENVELOPE_1, PATCH_SOURCE_Y},
};

void SoundEditor::setShortcutsVersion(int newVersion) {

	shortcutsVersion = newVersion;

#if ALPHA_OR_BETA_VERSION && IN_HARDWARE_DEBUG
	paramShortcutsForSounds[5][7] = &devVarAMenu;
	paramShortcutsForAudioClips[5][7] = &devVarAMenu;
#endif

	switch (newVersion) {

	case SHORTCUTS_VERSION_1:

		paramShortcutsForAudioClips[0][7] = &audioClipSampleMarkerEditorMenuStart;
		paramShortcutsForAudioClips[1][7] = &audioClipSampleMarkerEditorMenuStart;

		paramShortcutsForAudioClips[0][6] = &audioClipSampleMarkerEditorMenuEnd;
		paramShortcutsForAudioClips[1][6] = &audioClipSampleMarkerEditorMenuEnd;

		paramShortcutsForSounds[0][6] = &sampleEndMenu;
		paramShortcutsForSounds[1][6] = &sampleEndMenu;

		paramShortcutsForSounds[2][6] = &noiseMenu;
		paramShortcutsForSounds[3][6] = &oscSyncMenu;

		paramShortcutsForSounds[2][7] = &sourceWaveIndexMenu;
		paramShortcutsForSounds[3][7] = &sourceWaveIndexMenu;

		modSourceShortcuts[0][7] = 255;
		modSourceShortcuts[1][7] = 255;

		break;

	default: // VERSION_3
		// Uses defaults
		break;
	}
}

SoundEditor soundEditor;

SoundEditor::SoundEditor() {
	currentParamShorcutX = 255;
	memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
	timeLastAttemptedAutomatedParamEdit = 0;
	shouldGoUpOneLevelOnBegin = false;

#if HAVE_OLED
	init_menu_titles();
#endif
}

bool SoundEditor::editingKit() {
	return currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT;
}

bool SoundEditor::editingCVOrMIDIClip() {
	return (currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT
	        || currentSong->currentClip->output->type == INSTRUMENT_TYPE_CV);
}

bool SoundEditor::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (getRootUI() == &keyboardScreen) {
		return false;
	}
	else if (getRootUI() == &instrumentClipView) {
		*cols = 0xFFFFFFFE;
	}
	else {
		*cols = 0xFFFFFFFF;
	}
	return true;
}

bool SoundEditor::opened() {
	bool success =
	    beginScreen(); // Could fail for instance if going into WaveformView but sample not found on card, or going into SampleBrowser but card not present
	if (!success) {
		return true; // Must return true, which means everything is dealt with - because this UI would already have been exited if there was a problem
	}

	setLedStates();

	return true;
}

void SoundEditor::focusRegained() {

	// If just came back from a deeper nested UI...
	if (shouldGoUpOneLevelOnBegin) {
		goUpOneLevel();
		shouldGoUpOneLevelOnBegin = false;

		// If that already exited this UI, then get out now before setting any LEDs
		if (getCurrentUI() != this) {
			return;
		}

		PadLEDs::skipGreyoutFade();
	}
	else {
		beginScreen();
	}

	setLedStates();
}

void SoundEditor::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::SAVE, false); // In case we came from the save-Instrument UI

	indicator_leds::setLedState(IndicatorLED::SYNTH, !inSettingsMenu() && !editingKit() && currentSound);
	indicator_leds::setLedState(IndicatorLED::KIT, !inSettingsMenu() && editingKit() && currentSound);
	indicator_leds::setLedState(
	    IndicatorLED::MIDI, !inSettingsMenu() && currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT);
	indicator_leds::setLedState(IndicatorLED::CV,
	                            !inSettingsMenu() && currentSong->currentClip->output->type == INSTRUMENT_TYPE_CV);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	indicator_leds::blinkLed(IndicatorLED::BACK);

	playbackHandler.setLedStates();
}

int SoundEditor::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// Encoder button
	if (b == SELECT_ENC) {
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING) {
			if (on) {
				if (inCardRoutine) {
					return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				MenuItem* newItem = getCurrentMenuItem()->selectButtonPress();
				if (newItem) {
					if (newItem != (MenuItem*)0xFFFFFFFF) {

						int result = newItem->checkPermissionToBeginSession(currentSound, currentSourceIndex,
						                                                    &currentMultiRange);

						if (result != MENU_PERMISSION_NO) {

							if (result == MENU_PERMISSION_MUST_SELECT_RANGE) {
								currentMultiRange = NULL;
								menu_item::multiRangeMenu.menuItemHeadingTo = newItem;
								newItem = &menu_item::multiRangeMenu;
							}

							navigationDepth++;
							menuItemNavigationRecord[navigationDepth] = newItem;
							numericDriver.setNextTransitionDirection(1);
							beginScreen();
						}
					}
				}
				else {
					goUpOneLevel();
				}
			}
		}
	}

	// Back button
	else if (b == BACK) {
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING) {
			if (on) {
				if (inCardRoutine) {
					return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}

				// Special case if we're editing a range
				if (getCurrentMenuItem() == &menu_item::multiRangeMenu
				    && menu_item::multiRangeMenu.cancelEditingIfItsOn()) {}
				else {
					goUpOneLevel();
				}
			}
		}
	}

	// Save button
	else if (b == SAVE) {
		if (on && currentUIMode == UI_MODE_NONE && !inSettingsMenu() && !editingCVOrMIDIClip()
		    && currentSong->currentClip->type != CLIP_TYPE_AUDIO) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (Buttons::isShiftButtonPressed()) {
				if (getCurrentMenuItem() == &menu_item::multiRangeMenu) {
					menu_item::multiRangeMenu.deletePress();
				}
			}
			else {
				openUI(&saveInstrumentPresetUI);
			}
		}
	}

	// MIDI learn button
	else if (b == LEARN) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (on) {
			if (!currentUIMode) {
				if (!getCurrentMenuItem()->allowsLearnMode()) {
					numericDriver.displayPopup(HAVE_OLED ? "Can't learn" : "CANT");
				}
				else {
					if (Buttons::isShiftButtonPressed()) {
						getCurrentMenuItem()->unlearnAction();
					}
					else {
						indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1);
						currentUIMode = UI_MODE_MIDI_LEARN;
					}
				}
			}
		}
		else {
			if (getCurrentMenuItem()->shouldBlinkLearnLed()) {
				indicator_leds::blinkLed(IndicatorLED::LEARN);
			}
			else {
				indicator_leds::setLedState(IndicatorLED::LEARN, false);
			}

			if (currentUIMode == UI_MODE_MIDI_LEARN) {
				currentUIMode = UI_MODE_NONE;
			}
		}
	}

	// Affect-entire button
	else if (b == AFFECT_ENTIRE && getRootUI() == &instrumentClipView) {
		if (getCurrentMenuItem()->usesAffectEntire() && editingKit()) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (on) {
				if (currentUIMode == UI_MODE_NONE) {
					indicator_leds::blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1);
					currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
				}
			}
			else {
				if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
					view.setModLedStates();
					currentUIMode = UI_MODE_NONE;
				}
			}
		}
		else {
			return instrumentClipView.InstrumentClipMinder::buttonAction(b, on, inCardRoutine);
		}
	}

	// Keyboard button
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE && !editingKit()) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (getRootUI() == &keyboardScreen) {
				swapOutRootUILowLevel(&instrumentClipView);
				instrumentClipView.openedInBackground();
			}
			else if (getRootUI() == &instrumentClipView) {
				swapOutRootUILowLevel(&keyboardScreen);
				keyboardScreen.openedInBackground();
			}

			PadLEDs::reassessGreyout(true);

			indicator_leds::setLedState(IndicatorLED::KEYBOARD, getRootUI() == &keyboardScreen);
		}
	}

	else {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void SoundEditor::goUpOneLevel() {
	do {
		if (navigationDepth == 0) {
			exitCompletely();
			return;
		}
		navigationDepth--;
	} while (
	    !getCurrentMenuItem()->checkPermissionToBeginSession(currentSound, currentSourceIndex, &currentMultiRange));
	numericDriver.setNextTransitionDirection(-1);

	MenuItem* oldItem = menuItemNavigationRecord[navigationDepth + 1];
	if (oldItem == &menu_item::multiRangeMenu) {
		oldItem = menu_item::multiRangeMenu.menuItemHeadingTo;
	}

	beginScreen(oldItem);
}

void SoundEditor::exitCompletely() {
	if (inSettingsMenu()) {
		// First, save settings
#if HAVE_OLED
		OLED::displayWorkingAnimation("Saving settings");
#else
		numericDriver.displayLoadingAnimation();
#endif
		FlashStorage::writeSettings();
		MIDIDeviceManager::writeDevicesToFile();
		runtimeFeatureSettings.writeSettingsToFile();
#if HAVE_OLED
		OLED::removeWorkingAnimation();
#endif
	}
	numericDriver.setNextTransitionDirection(-1);
	close();
	possibleChangeToCurrentRangeDisplay();
}

bool SoundEditor::beginScreen(MenuItem* oldMenuItem) {

	MenuItem* currentItem = getCurrentMenuItem();

	currentItem->beginSession(oldMenuItem);

	// If that didn't succeed (file browser)
	if (getCurrentUI() != &soundEditor && getCurrentUI() != &sampleBrowser && getCurrentUI() != &audioRecorder
	    && getCurrentUI() != &sampleMarkerEditor && getCurrentUI() != &renameDrumUI) {
		return false;
	}

#if HAVE_OLED
	renderUIsForOled();
#endif

	if (!inSettingsMenu() && currentItem != &sampleStartMenu && currentItem != &sampleEndMenu
	    && currentItem != &audioClipSampleMarkerEditorMenuStart && currentItem != &audioClipSampleMarkerEditorMenuEnd
	    && currentItem != &fileSelectorMenu && currentItem != static_cast<void*>(&drumNameMenu)) {

		memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
		memset(sourceShortcutBlinkColours, 0, sizeof(sourceShortcutBlinkColours));
		paramShortcutBlinkFrequency = 3;

		// Find param shortcut
		currentParamShorcutX = 255;

		// For AudioClips...
		if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {

			int x, y;

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (x = 0; x < 15; x++) {
				for (y = 0; y < displayHeight; y++) {
					if (paramShortcutsForAudioClips[x][y] == currentItem) {
						//if (x == 10 && y < 6 && editingReverbCompressor()) goto stopThat;
						//if (currentParamShorcutX != 255 && (x & 1) && currentSourceIndex == 0) goto stopThat;
						goto doSetupBlinkingForAudioClip;
					}
				}
			}

			if (false) {
doSetupBlinkingForAudioClip:
				setupShortcutBlink(x, y, 0);
			}
		}

		// Or for MIDI or CV clips
		else if (editingCVOrMIDIClip()) {
			for (int y = 0; y < displayHeight; y++) {
				if (midiOrCVParamShortcuts[y] == currentItem) {
					setupShortcutBlink(11, y, 0);
					break;
				}
			}
		}

		// Or the "normal" case, for Sounds
		else {

			if (currentItem == &menu_item::multiRangeMenu) {
				currentItem = menu_item::multiRangeMenu.menuItemHeadingTo;
			}

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (int x = 0; x < 15; x++) {
				for (int y = 0; y < displayHeight; y++) {
					if (paramShortcutsForSounds[x][y] == currentItem) {

						if (x == 10 && y < 6 && editingReverbCompressor()) {
							goto stopThat;
						}

						if (currentParamShorcutX != 255 && (x & 1) && currentSourceIndex == 0) {
							goto stopThat;
						}
						setupShortcutBlink(x, y, 0);
					}
				}
			}

			// Failing that, if we're doing some patching, see if there's a shortcut for that *param*
			if (currentParamShorcutX == 255) {

				int paramLookingFor = currentItem->getIndexOfPatchedParamToBlink();
				if (paramLookingFor != 255) {
					for (int x = 0; x < 15; x++) {
						for (int y = 0; y < displayHeight; y++) {
							if (paramShortcutsForSounds[x][y] && paramShortcutsForSounds[x][y] != comingSoonMenu
							    && ((MenuItem*)paramShortcutsForSounds[x][y])->getPatchedParamIndex()
							           == paramLookingFor) {

								if (currentParamShorcutX != 255 && (x & 1) && currentSourceIndex == 0) {
									goto stopThat;
								}

								setupShortcutBlink(x, y, 3);
							}
						}
					}
				}
			}

stopThat : {}

			if (currentParamShorcutX != 255) {
				for (int x = 0; x < 2; x++) {
					for (int y = 0; y < displayHeight; y++) {
						uint8_t source = modSourceShortcuts[x][y];
						if (source < NUM_PATCH_SOURCES) {
							sourceShortcutBlinkFrequencies[x][y] = currentItem->shouldBlinkPatchingSourceShortcut(
							    source, &sourceShortcutBlinkColours[x][y]);
						}
					}
				}
			}
		}

		// If we found nothing...
		if (currentParamShorcutX == 255) {
			uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
		}

		// Or if we found something...
		else {
			blinkShortcut();
		}
	}

shortcutsPicked:

	if (currentItem->shouldBlinkLearnLed()) {
		indicator_leds::blinkLed(IndicatorLED::LEARN);
	}
	else {
		indicator_leds::setLedState(IndicatorLED::LEARN, false);
	}

	possibleChangeToCurrentRangeDisplay();

	return true;
}

void SoundEditor::possibleChangeToCurrentRangeDisplay() {
	uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
	uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
}

void SoundEditor::setupShortcutBlink(int x, int y, int frequency) {
	currentParamShorcutX = x;
	currentParamShorcutY = y;

	shortcutBlinkCounter = 0;
	paramShortcutBlinkFrequency = frequency;
}

void SoundEditor::setupExclusiveShortcutBlink(int x, int y) {
	memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
	setupShortcutBlink(x, y, 1);
	blinkShortcut();
}

void SoundEditor::blinkShortcut() {
	// We have to blink params and shortcuts at slightly different times, because blinking two pads on the same row at same time doesn't work

	uint32_t counterForNow = shortcutBlinkCounter >> 1;

	if (shortcutBlinkCounter & 1) {
		// Blink param
		if ((counterForNow & paramShortcutBlinkFrequency) == 0) {
			PadLEDs::flashMainPad(currentParamShorcutX, currentParamShorcutY);
		}
		uiTimerManager.setTimer(TIMER_SHORTCUT_BLINK, 180);
	}

	else {
		// Blink source
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < displayHeight; y++) {
				if (sourceShortcutBlinkFrequencies[x][y] != 255
				    && (counterForNow & sourceShortcutBlinkFrequencies[x][y]) == 0) {
					PadLEDs::flashMainPad(x + 14, y, sourceShortcutBlinkColours[x][y]);
				}
			}
		}
		uiTimerManager.setTimer(TIMER_SHORTCUT_BLINK, 20);
	}

	shortcutBlinkCounter++;
}

bool SoundEditor::editingReverbCompressor() {
	return (getCurrentUI() == &soundEditor && currentCompressor == &AudioEngine::reverbCompressor);
}

int SoundEditor::horizontalEncoderAction(int offset) {
	if (currentUIMode == UI_MODE_AUDITIONING && getRootUI() == &keyboardScreen) {
		return getRootUI()->horizontalEncoderAction(offset);
	}
	else {
		getCurrentMenuItem()->horizontalEncoderAction(offset);
		return ACTION_RESULT_DEALT_WITH;
	}
}

void SoundEditor::selectEncoderAction(int8_t offset) {

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_AUDITIONING
	    && currentUIMode != UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
		return;
	}

	bool hadNoteTails;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();

	if (currentSound) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();

		hadNoteTails = currentSound->allowNoteTails(modelStack);
	}

	getCurrentMenuItem()->selectEncoderAction(offset);

	if (currentSound) {
		if (getCurrentMenuItem()->selectEncoderActionEditsInstrument()) {
			markInstrumentAsEdited(); // TODO: make reverb and reverb-compressor stuff exempt from this
		}

		// If envelope param preset values were changed, there's a chance that there could have been a change to whether notes have tails
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = getCurrentModelStack(modelStackMemory)->addSoundFlags();

		bool hasNoteTailsNow = currentSound->allowNoteTails(modelStack);
		if (hadNoteTails != hasNoteTailsNow) {
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		}
	}

	if (currentModControllable) {
		view.setKnobIndicatorLevels(); // Is this really necessary every time?
	}
}

void SoundEditor::markInstrumentAsEdited() {
	if (!inSettingsMenu()) {
		((Instrument*)currentSong->currentClip->output)->beenEdited();
	}
}

static const uint32_t shortcutPadUIModes[] = {UI_MODE_AUDITIONING, 0};

int SoundEditor::potentialShortcutPadAction(int x, int y, bool on) {

	if (!on || x >= displayWidth
	    || (!Buttons::isShiftButtonPressed()
	        && !(currentUIMode == UI_MODE_AUDITIONING && getRootUI() == &instrumentClipView))) {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	if (on && isUIModeWithinRange(shortcutPadUIModes)) {

		if (sdRoutineLock) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		const MenuItem* item = NULL;

		// AudioClips - there are just a few shortcuts
		if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {

			if (x <= 14) {
				item = paramShortcutsForAudioClips[x][y];
			}

			goto doSetup;
		}

		else {
			// Shortcut to edit a parameter
			if (x < 14 || (x == 14 && y < 5)) {

				if (editingCVOrMIDIClip()) {
					if (x == 11) {
						item = midiOrCVParamShortcuts[y];
					}
					else if (x == 4) {
						if (y == 7) {
							item = &sequenceDirectionMenu;
						}
					}
					else {
						item = NULL;
					}
				}
				else {
					item = paramShortcutsForSounds[x][y];
				}

doSetup:
				if (item) {

					if (item == comingSoonMenu) {
						numericDriver.displayPopup(HAVE_OLED ? "Feature not (yet?) implemented" : "SOON");
						return ACTION_RESULT_DEALT_WITH;
					}

#if HAVE_OLED
					switch (x) {
					case 0 ... 3:
						setOscillatorNumberForTitles(x & 1);
						break;

					case 4 ... 5:
						setModulatorNumberForTitles(x & 1);
						break;

					case 8 ... 9:
						setEnvelopeNumberForTitles(x & 1);
						break;
					}
#endif
					int thingIndex = x & 1;

					bool setupSuccess = setup((currentSong->currentClip), item, thingIndex);

					if (!setupSuccess) {
						return ACTION_RESULT_DEALT_WITH;
					}

					// If not in SoundEditor yet
					if (getCurrentUI() != &soundEditor) {
						if (getCurrentUI() == &sampleMarkerEditor) {
							numericDriver.setNextTransitionDirection(0);
							changeUIAtLevel(&soundEditor, 1);
							renderingNeededRegardlessOfUI(); // Not sure if this is 100% needed... some of it is.
						}
						else {
							openUI(&soundEditor);
						}
					}

					// Or if already in SoundEditor
					else {
						numericDriver.setNextTransitionDirection(0);
						beginScreen();
					}
				}
			}

			// Shortcut to patch a modulation source to the parameter we're already looking at
			else if (getCurrentUI() == &soundEditor) {

				uint8_t source = modSourceShortcuts[x - 14][y];
				if (source == 254) {
					numericDriver.displayPopup("SOON");
				}

				if (source >= NUM_PATCH_SOURCES) {
					return ACTION_RESULT_DEALT_WITH;
				}

				bool previousPressStillActive = false;
				for (int h = 0; h < 2; h++) {
					for (int i = 0; i < displayHeight; i++) {
						if (h == 0 && i < 5) {
							continue;
						}

						if ((h + 14 != x || i != y) && matrixDriver.isPadPressed(14 + h, i)) {
							previousPressStillActive = true;
							goto getOut;
						}
					}
				}

getOut:
				bool wentBack = false;

				int newNavigationDepth = navigationDepth;

				while (true) {

					// Ask current MenuItem what to do with this action
					MenuItem* newMenuItem = menuItemNavigationRecord[newNavigationDepth]->patchingSourceShortcutPress(
					    source, previousPressStillActive);

					// If it says "go up a level and ask that MenuItem", do that
					if (newMenuItem == (MenuItem*)0xFFFFFFFF) {
						newNavigationDepth--;
						if (newNavigationDepth < 0) { // This normally shouldn't happen
							exitCompletely();
							return ACTION_RESULT_DEALT_WITH;
						}
						wentBack = true;
					}

					// Otherwise...
					else {

						// If we've been given a MenuItem to go into, do that
						if (newMenuItem
						    && newMenuItem->checkPermissionToBeginSession(currentSound, currentSourceIndex,
						                                                  &currentMultiRange)) {
							navigationDepth = newNavigationDepth + 1;
							menuItemNavigationRecord[navigationDepth] = newMenuItem;
							if (!wentBack) {
								numericDriver.setNextTransitionDirection(1);
							}
							beginScreen();
						}

						// Otherwise, do nothing
						break;
					}
				}
			}
		}
	}
	return ACTION_RESULT_DEALT_WITH;
}

extern uint16_t batteryMV;

int SoundEditor::padAction(int x, int y, int on) {
	if (sdRoutineLock) {
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (!inSettingsMenu()) {
		int result = potentialShortcutPadAction(x, y, on);
		if (result != ACTION_RESULT_NOT_DEALT_WITH) {
			return result;
		}
	}

	if (getRootUI() == &keyboardScreen) {
		if (x < displayWidth) {
			keyboardScreen.padAction(x, y, on);
			return ACTION_RESULT_DEALT_WITH;
		}
	}

	// Audition pads
	else if (getRootUI() == &instrumentClipView) {
		if (x == displayWidth + 1) {
			instrumentClipView.padAction(x, y, on);
			return ACTION_RESULT_DEALT_WITH;
		}
	}

	// Otherwise...
	if (currentUIMode == UI_MODE_NONE && on) {

		// If doing secret bootloader-update action...
		// Dear tinkerers and open-sourcers, please don't use or publicise this feature. If it goes wrong, your Deluge is toast.
		if (getCurrentMenuItem() == &firmwareVersionMenu
		    && ((x == 0 && y == 7) || (x == 1 && y == 6) || (x == 2 && y == 5))) {

			if (matrixDriver.isUserDoingBootloaderOverwriteAction()) {
				bool available = gui::context_menu::overwriteBootloader.setupAndCheckAvailability();
				if (available) {
					openUI(&gui::context_menu::overwriteBootloader);
				}
			}
		}

		// Otherwise, exit.
		else {
			exitCompletely();
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

int SoundEditor::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(hid::button::X_ENC)) {
		return ACTION_RESULT_DEALT_WITH;
	}
	return getRootUI()->verticalEncoderAction(offset, inCardRoutine);
}

bool SoundEditor::noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int channel, int note, int velocity) {
	return getCurrentMenuItem()->learnNoteOn(fromDevice, channel, note);
}

// Returns true if some use was made of the message here
bool SoundEditor::midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value) {

	if (currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()) {
		getCurrentMenuItem()->learnCC(fromDevice, channel, ccNumber, value);
		return true;
	}

	return false;
}

// Returns true if some use was made of the message here
bool SoundEditor::pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2) {

	if (currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()) {
		getCurrentMenuItem()->learnKnob(fromDevice, 128, 0, channel);
		return true;
	}

	return false;
}

void SoundEditor::modEncoderAction(int whichModEncoder, int offset) {
	// If learn button is pressed, learn this knob for current param
	if (currentUIMode == UI_MODE_MIDI_LEARN) {

		// But, can't do it if it's a Kit and affect-entire is on!
		if (editingKit() && ((InstrumentClip*)currentSong->currentClip)->affectEntire) {
			//IndicatorLEDs::indicateErrorOnLed(affectEntireLedX, affectEntireLedY);
		}

		// Otherwise, everything's fine
		else {
			getCurrentMenuItem()->learnKnob(NULL, whichModEncoder, currentSong->currentClip->output->modKnobMode, 255);
		}
	}

	// Otherwise, send the action to the Editor as usual
	else {
		UI::modEncoderAction(whichModEncoder, offset);
	}
}

bool SoundEditor::setup(Clip* clip, const MenuItem* item, int sourceIndex) {

	Sound* newSound = NULL;
	ParamManagerForTimeline* newParamManager = NULL;
	ArpeggiatorSettings* newArpSettings = NULL;
	ModControllableAudio* newModControllable = NULL;

	if (clip) {

		// InstrumentClips
		if (clip->type == CLIP_TYPE_INSTRUMENT) {
			// Kit
			if (clip->output->type == INSTRUMENT_TYPE_KIT) {

				Drum* selectedDrum = ((Kit*)clip->output)->selectedDrum;
				// If a SoundDrum is selected...
				if (selectedDrum) {
					if (selectedDrum->type == DRUM_TYPE_SOUND) {
						NoteRow* noteRow = ((InstrumentClip*)clip)->getNoteRowForDrum(selectedDrum);
						if (noteRow == NULL) {
							return false;
						}
						newSound = (SoundDrum*)selectedDrum;
						newModControllable = newSound;
						newParamManager = &noteRow->paramManager;
						newArpSettings = &((SoundDrum*)selectedDrum)->arpSettings;
					}
					else {
						if (item != &sequenceDirectionMenu) {
							if (selectedDrum->type == DRUM_TYPE_MIDI) {
								indicator_leds::indicateAlertOnLed(IndicatorLED::MIDI);
							}
							else { // GATE
								indicator_leds::indicateAlertOnLed(IndicatorLED::CV);
							}
							return false;
						}
					}
				}

				// Otherwise, do nothing
				else {
					if (item == &sequenceDirectionMenu) {
						numericDriver.displayPopup(HAVE_OLED ? "Select a row or affect-entire" : "CANT");
					}
					return false;
				}
			}

			else {

				// Synth
				if (clip->output->type == INSTRUMENT_TYPE_SYNTH) {
					newSound = (SoundInstrument*)clip->output;
					newModControllable = newSound;
				}

				// CV or MIDI - not much happens

				newParamManager = &clip->paramManager;
				newArpSettings = &((InstrumentClip*)clip)->arpSettings;
			}
		}

		// AudioClips
		else {
			newParamManager = &clip->paramManager;
			newModControllable = (ModControllableAudio*)clip->output->toModControllable();
		}
	}

	MenuItem* newItem;

	if (item) {
		newItem = (MenuItem*)item;
	}
	else {
		if (clip) {

			actionLogger.deleteAllLogs();

			if (clip->type == CLIP_TYPE_INSTRUMENT) {
				if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT) {
#if HAVE_OLED
					soundEditorRootMenuMIDIOrCV.basicTitle = "MIDI inst.";
#endif
doMIDIOrCV:
					newItem = &soundEditorRootMenuMIDIOrCV;
				}
				else if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_CV) {
#if HAVE_OLED
					soundEditorRootMenuMIDIOrCV.basicTitle = "CV instrument";
#endif
					goto doMIDIOrCV;
				}
				else {
					newItem = &soundEditorRootMenu;
				}
			}

			else {
				newItem = &soundEditorRootMenuAudioClip;
			}
		}
		else {
			newItem = &settingsRootMenu;
		}
	}

	::MultiRange* newRange = currentMultiRange;

	if ((getCurrentUI() != &soundEditor && getCurrentUI() != &sampleMarkerEditor)
	    || sourceIndex != currentSourceIndex) {
		newRange = NULL;
	}

	// This isn't a very nice solution, but we have to set currentParamManager before calling checkPermissionToBeginSession(),
	// because in a minority of cases, like "patch cable strength" / "modulation depth", it needs this.
	currentParamManager = newParamManager;

	int result = newItem->checkPermissionToBeginSession(newSound, sourceIndex, &newRange);

	if (result == MENU_PERMISSION_NO) {
		numericDriver.displayPopup(HAVE_OLED ? "Parameter not applicable" : "CANT");
		return false;
	}
	else if (result == MENU_PERMISSION_MUST_SELECT_RANGE) {

		Debug::println("must select range");

		newRange = NULL;
		menu_item::multiRangeMenu.menuItemHeadingTo = newItem;
		newItem = &menu_item::multiRangeMenu;
	}

	currentSound = newSound;
	currentArpSettings = newArpSettings;
	currentMultiRange = newRange;
	currentModControllable = newModControllable;

	if (currentModControllable) {
		currentCompressor = &currentModControllable->compressor;
	}

	if (currentSound) {
		currentSourceIndex = sourceIndex;
		currentSource = &currentSound->sources[currentSourceIndex];
		currentSampleControls = &currentSource->sampleControls;
		currentPriority = &currentSound->voicePriority;

		if (result == MENU_PERMISSION_YES && currentMultiRange == NULL) {
			if (currentSource->ranges.getNumElements()) {
				currentMultiRange = (MultisampleRange*)currentSource->ranges.getElement(0); // Is this good?
			}
		}
	}
	else if (clip->type == CLIP_TYPE_AUDIO) {
		AudioClip* audioClip = (AudioClip*)clip;
		currentSampleControls = &audioClip->sampleControls;
		currentPriority = &audioClip->voicePriority;
	}

	navigationDepth = 0;
	shouldGoUpOneLevelOnBegin = false;
	menuItemNavigationRecord[navigationDepth] = newItem;

	numericDriver.setNextTransitionDirection(1);
	return true;
}

MenuItem* SoundEditor::getCurrentMenuItem() {
	return menuItemNavigationRecord[navigationDepth];
}

bool SoundEditor::inSettingsMenu() {
	return (menuItemNavigationRecord[0] == &settingsRootMenu);
}

bool SoundEditor::isUntransposedNoteWithinRange(int noteCode) {
	return (soundEditor.currentSource->ranges.getNumElements() > 1
	        && soundEditor.currentSource->getRange(noteCode + soundEditor.currentSound->transpose)
	               == soundEditor.currentMultiRange);
}

void SoundEditor::setCurrentMultiRange(int i) {
	currentMultiRangeIndex = i;
	currentMultiRange = (MultisampleRange*)soundEditor.currentSource->ranges.getElement(i);
}

int SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam(Sound* sound, int whichThing,
                                                                    bool automaticallySelectIfOnlyOne,
                                                                    ::MultiRange** previouslySelectedRange) {

	Source* source = &sound->sources[whichThing];

	::MultiRange* firstRange = source->getOrCreateFirstRange();
	if (!firstRange) {
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return MENU_PERMISSION_NO;
	}

	if (soundEditor.editingKit() || (automaticallySelectIfOnlyOne && source->ranges.getNumElements() == 1)) {
		*previouslySelectedRange = firstRange;
		return MENU_PERMISSION_YES;
	}

	if (getCurrentUI() == &soundEditor && *previouslySelectedRange && currentSourceIndex == whichThing) {
		return MENU_PERMISSION_YES;
	}

	return MENU_PERMISSION_MUST_SELECT_RANGE;
}

void SoundEditor::cutSound() {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		((AudioClip*)currentSong->currentClip)->unassignVoiceSample();
	}
	else {
		soundEditor.currentSound->unassignAllVoices();
	}
}

AudioFileHolder* SoundEditor::getCurrentAudioFileHolder() {

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return &((AudioClip*)currentSong->currentClip)->sampleHolder;
	}

	else {
		return currentMultiRange->getAudioFileHolder();
	}
}

ModelStackWithThreeMainThings* SoundEditor::getCurrentModelStack(void* memory) {
	NoteRow* noteRow = NULL;
	int noteRowIndex;

	if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
		Drum* selectedDrum = ((Kit*)currentSong->currentClip->output)->selectedDrum;
		if (selectedDrum) {
			noteRow = ((InstrumentClip*)currentSong->currentClip)->getNoteRowForDrum(selectedDrum, &noteRowIndex);
		}
	}

	return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, currentSong->currentClip,
	                                                          noteRowIndex, noteRow, currentModControllable,
	                                                          currentParamManager);
}

void SoundEditor::mpeZonesPotentiallyUpdated() {
	if (getCurrentUI() == this) {
		MenuItem* currentMenuItem = getCurrentMenuItem();
		if (currentMenuItem == &mpe::zoneNumMemberChannelsMenu) {
			currentMenuItem->readValueAgain();
		}
	}
}

#if HAVE_OLED
void SoundEditor::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	// Sorry - extremely ugly hack here.
	MenuItem* currentMenuItem = getCurrentMenuItem();
	if (currentMenuItem == static_cast<void*>(&drumNameMenu)) {
		if (!navigationDepth) {
			return;
		}
		currentMenuItem = menuItemNavigationRecord[navigationDepth - 1];
	}

	currentMenuItem->renderOLED();
}
#endif

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
*/
