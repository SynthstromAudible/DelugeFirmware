
#include "gui/ui/sound_editor.h"
#include "definitions_cxx.hpp"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "extern.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/rename/rename_output_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/voice/voice_vector.h"
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

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

#include "menus.h"

using namespace deluge;
using namespace deluge::gui;
using namespace deluge::gui::menu_item;

#define comingSoonMenu (MenuItem*)0xFFFFFFFF

// 255 means none. 254 means soon
PatchSource modSourceShortcuts[2][8] = {
    {
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::NOT_AVAILABLE,
        PatchSource::LFO_GLOBAL,
        PatchSource::ENVELOPE_0,
        PatchSource::X,
    },
    {
        PatchSource::AFTERTOUCH,
        PatchSource::VELOCITY,
        PatchSource::RANDOM,
        PatchSource::NOTE,
        PatchSource::COMPRESSOR,
        PatchSource::LFO_LOCAL,
        PatchSource::ENVELOPE_1,
        PatchSource::Y,
    },
};

void SoundEditor::setShortcutsVersion(int32_t newVersion) {

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

		modSourceShortcuts[0][7] = PatchSource::NOT_AVAILABLE;
		modSourceShortcuts[1][7] = PatchSource::NOT_AVAILABLE;

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
	setupKitGlobalFXMenu = false;
}

bool SoundEditor::editingKit() {
	return getCurrentInstrumentType() == InstrumentType::KIT;
}

bool SoundEditor::editingCVOrMIDIClip() {
	return (getCurrentInstrumentType() == InstrumentType::MIDI_OUT || getCurrentInstrumentType() == InstrumentType::CV);
}

bool SoundEditor::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (getRootUI() == &keyboardScreen) {
		return false;
	}
	else if (getRootUI() == &automationInstrumentClipView || getRootUI() == &instrumentClipView) {
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

	//update save button blinking status when in performance session view
	if (getRootUI() == &performanceSessionView) {
		performanceSessionView.updateLayoutChangeStatus();
	}

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

	//update save button blinking status when in performance session view
	if (getRootUI() == &performanceSessionView) {
		performanceSessionView.updateLayoutChangeStatus();
	}
}

void SoundEditor::displayOrLanguageChanged() {
	getCurrentMenuItem()->readValueAgain();
}

void SoundEditor::setLedStates() {
	indicator_leds::setLedState(IndicatorLED::SAVE, false); // In case we came from the save-Instrument UI

	indicator_leds::setLedState(IndicatorLED::SYNTH, !inSettingsMenu() && !editingKit() && currentSound);
	indicator_leds::setLedState(IndicatorLED::KIT, !inSettingsMenu() && editingKit() && currentSound);
	indicator_leds::setLedState(IndicatorLED::MIDI,
	                            !inSettingsMenu() && getCurrentInstrumentType() == InstrumentType::MIDI_OUT);
	indicator_leds::setLedState(IndicatorLED::CV,
	                            !inSettingsMenu() && getCurrentInstrumentType() == InstrumentType::CV);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	indicator_leds::blinkLed(IndicatorLED::BACK);

	playbackHandler.setLedStates();
}

ActionResult SoundEditor::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Encoder button
	if (b == SELECT_ENC) {
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING
		    || getRootUI() == &performanceSessionView) {
			if (on) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				MenuItem* newItem = getCurrentMenuItem()->selectButtonPress();
				if (newItem) {
					if (newItem != (MenuItem*)0xFFFFFFFF) {

						MenuPermission result = newItem->checkPermissionToBeginSession(currentSound, currentSourceIndex,
						                                                               &currentMultiRange);

						if (result != MenuPermission::NO) {

							if (result == MenuPermission::MUST_SELECT_RANGE) {
								currentMultiRange = NULL;
								menu_item::multiRangeMenu.menuItemHeadingTo = newItem;
								newItem = &menu_item::multiRangeMenu;
							}

							navigationDepth++;
							menuItemNavigationRecord[navigationDepth] = newItem;
							display->setNextTransitionDirection(1);
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
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING
		    || getRootUI() == &performanceSessionView) {
			if (on) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
		if (on && (currentUIMode == UI_MODE_NONE) && !inSettingsMenu() && !inSongMenu() && !editingCVOrMIDIClip()
		    && getCurrentClip()->type != CLIP_TYPE_AUDIO) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (on) {
			if (!currentUIMode) {
				if (!getCurrentMenuItem()->allowsLearnMode()) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_LEARN));
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
	else if (b == AFFECT_ENTIRE
	         && (getRootUI() == &instrumentClipView || getRootUI() == &automationInstrumentClipView)) {
		if (getCurrentMenuItem()->usesAffectEntire() && editingKit()) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (getRootUI() == &keyboardScreen) {

				if (getCurrentInstrumentClip()->onAutomationInstrumentClipView) {
					swapOutRootUILowLevel(&automationInstrumentClipView);
					automationInstrumentClipView.openedInBackground();
				}

				else {
					swapOutRootUILowLevel(&instrumentClipView);
					instrumentClipView.openedInBackground();
				}
			}
			else if (getRootUI() == &instrumentClipView) {
				swapOutRootUILowLevel(&keyboardScreen);
				keyboardScreen.openedInBackground();
			}

			else if (getRootUI() == &automationInstrumentClipView) {
				swapOutRootUILowLevel(&keyboardScreen);
				keyboardScreen.openedInBackground();
			}

			if (getRootUI() != &performanceSessionView) {
				PadLEDs::reassessGreyout(true);

				indicator_leds::setLedState(IndicatorLED::KEYBOARD, getRootUI() == &keyboardScreen);
			}
		}
	}

	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void SoundEditor::goUpOneLevel() {
	do {
		if (navigationDepth == 0) {
			exitCompletely();
			return;
		}
		navigationDepth--;
	} while (getCurrentMenuItem()->checkPermissionToBeginSession(currentSound, currentSourceIndex, &currentMultiRange)
	         == MenuPermission::NO);
	display->setNextTransitionDirection(-1);

	MenuItem* oldItem = menuItemNavigationRecord[navigationDepth + 1];
	if (oldItem == &menu_item::multiRangeMenu) {
		oldItem = menu_item::multiRangeMenu.menuItemHeadingTo;
	}

	beginScreen(oldItem);
}

void SoundEditor::exitCompletely() {
	if (inSettingsMenu()) {
		// First, save settings

		display->displayLoadingAnimationText("Saving settings");

		FlashStorage::writeSettings();
		MIDIDeviceManager::writeDevicesToFile();
		runtimeFeatureSettings.writeSettingsToFile();
		display->removeWorkingAnimation();
	}
	display->setNextTransitionDirection(-1);
	close();
	possibleChangeToCurrentRangeDisplay();

	// a bit ad-hoc but the current memory allocator
	// is not happy with these strings being around
	patchCablesMenu.options.clear();

	//don't save any of the logs created while using the sound editor to edit param values
	//in performance view value editing mode
	if ((getRootUI() == &performanceSessionView) && (performanceSessionView.defaultEditingMode)) {
		actionLogger.deleteAllLogs();
	}

	setupKitGlobalFXMenu = false;
}

bool SoundEditor::findPatchedParam(int32_t paramLookingFor, int32_t* xout, int32_t* yout) {
	bool found = false;
	for (int32_t x = 0; x < 15; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (paramShortcutsForSounds[x][y] && paramShortcutsForSounds[x][y] != comingSoonMenu
			    && ((MenuItem*)paramShortcutsForSounds[x][y])->getPatchedParamIndex() == paramLookingFor) {

				*xout = x;
				*yout = y;

				if ((x & 1) == currentSourceIndex) {
					//check we're on the corretc index
					return true;
				}
			}
		}
	}
	return found;
}

void SoundEditor::updateSourceBlinks(MenuItem* currentItem) {
	for (int32_t x = 0; x < 2; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			PatchSource source = modSourceShortcuts[x][y];
			if (source < kLastPatchSource) {
				sourceShortcutBlinkFrequencies[x][y] =
				    currentItem->shouldBlinkPatchingSourceShortcut(source, &sourceShortcutBlinkColours[x][y]);
			}
		}
	}
}

bool SoundEditor::beginScreen(MenuItem* oldMenuItem) {

	MenuItem* currentItem = getCurrentMenuItem();

	currentItem->beginSession(oldMenuItem);

	// If that didn't succeed (file browser)
	if (getCurrentUI() != &soundEditor && getCurrentUI() != &sampleBrowser && getCurrentUI() != &audioRecorder
	    && getCurrentUI() != &sampleMarkerEditor && getCurrentUI() != &renameDrumUI) {
		return false;
	}

	if (display->haveOLED()) {
		renderUIsForOled();
	}

	if (!inSettingsMenu() && currentItem != &sampleStartMenu && currentItem != &sampleEndMenu
	    && currentItem != &audioClipSampleMarkerEditorMenuStart && currentItem != &audioClipSampleMarkerEditorMenuEnd
	    && currentItem != &fileSelectorMenu && currentItem != static_cast<void*>(&drumNameMenu)) {

		memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
		memset(sourceShortcutBlinkColours, 0, sizeof(sourceShortcutBlinkColours));
		paramShortcutBlinkFrequency = 3;

		// Find param shortcut
		currentParamShorcutX = 255;
		bool isUISessionView =
		    (getRootUI() == &performanceSessionView) || (getRootUI() == &sessionView) || (getRootUI() == &arrangerView);

		if (isUISessionView) {
			int32_t x, y;

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (x = 0; x < 15; x++) {
				for (y = 0; y < kDisplayHeight; y++) {
					if (paramShortcutsForSongView[x][y] == currentItem) {
						goto doSetupBlinkingForSessionView;
					}
				}
			}

			if (false) {
doSetupBlinkingForSessionView:
				setupShortcutBlink(x, y, 0);
			}
		}

		//For Kit Instrument Clip with Affect Entire Enabled
		else if ((getCurrentInstrumentType() == InstrumentType::KIT) && (getCurrentInstrumentClip()->affectEntire)) {

			int32_t x, y;

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (x = 0; x < 15; x++) {
				for (y = 0; y < kDisplayHeight; y++) {
					if (paramShortcutsForKitGlobalFX[x][y] == currentItem) {
						goto doSetupBlinkingForKitGlobalFX;
					}
				}
			}

			if (false) {
doSetupBlinkingForKitGlobalFX:
				setupShortcutBlink(x, y, 0);
			}
		}

		// For AudioClips...
		else if (getCurrentClip()->type == CLIP_TYPE_AUDIO) {

			int32_t x, y;

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (x = 0; x < 15; x++) {
				for (y = 0; y < kDisplayHeight; y++) {
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
			for (int32_t y = 0; y < kDisplayHeight; y++) {
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
			for (int32_t x = 0; x < 15; x++) {
				for (int32_t y = 0; y < kDisplayHeight; y++) {
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

				int32_t paramLookingFor = currentItem->getIndexOfPatchedParamToBlink();
				if (paramLookingFor != 255) {
					int32_t x, y;
					if (findPatchedParam(paramLookingFor, &x, &y)) {
						setupShortcutBlink(x, y, 3);
					}
				}
			}

stopThat : {}

			if (currentParamShorcutX != 255) {
				updateSourceBlinks(currentItem);
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
	uiNeedsRendering(&automationInstrumentClipView, 0, 0xFFFFFFFF);
	uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
}

void SoundEditor::setupShortcutBlink(int32_t x, int32_t y, int32_t frequency) {
	currentParamShorcutX = x;
	currentParamShorcutY = y;

	shortcutBlinkCounter = 0;
	paramShortcutBlinkFrequency = frequency;
}

void SoundEditor::setupExclusiveShortcutBlink(int32_t x, int32_t y) {
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
		for (int32_t x = 0; x < 2; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
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

ActionResult SoundEditor::horizontalEncoderAction(int32_t offset) {
	if (currentUIMode == UI_MODE_AUDITIONING && getRootUI() == &keyboardScreen) {
		return getRootUI()->horizontalEncoderAction(offset);
	}
	else {
		getCurrentMenuItem()->horizontalEncoderAction(offset);
		return ActionResult::DEALT_WITH;
	}
}

void SoundEditor::selectEncoderAction(int8_t offset) {

	//5x acceleration of select encoder when holding the shift button
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT)) {
		offset = offset * 5;
	}

	//if you're in the performance view, let it handle the select encoder action
	if (getRootUI() == &performanceSessionView) {
		performanceSessionView.selectEncoderAction(offset);
	}
	else {
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
}

// TIMER_UI_SPECIFIC is only set by a menu item
ActionResult SoundEditor::timerCallback() {
	return getCurrentMenuItem()->timerCallback();
}

void SoundEditor::markInstrumentAsEdited() {
	if (!inSettingsMenu()) {
		Instrument* inst = getCurrentInstrumentOrNull();
		if (inst) {
			getCurrentInstrument()->beenEdited();
		}
	}
}

static const uint32_t shortcutPadUIModes[] = {UI_MODE_AUDITIONING, 0};

ActionResult SoundEditor::potentialShortcutPadAction(int32_t x, int32_t y, bool on) {

	bool ignoreAction = false;
	//if in Performance Session View
	if ((getRootUI() == &performanceSessionView) || (getCurrentUI() == &performanceSessionView)) {
		//ignore if you're not in editing mode or if you're in editing mode but editing a param
		ignoreAction = (!performanceSessionView.defaultEditingMode || performanceSessionView.editingParam);
	}
	else {
		//ignore if you're not auditioning and in instrument clip view
		ignoreAction = !(isUIModeActive(UI_MODE_AUDITIONING) && getRootUI() == &instrumentClipView);
	}

	// ignore if:
	// A) velocity is off (you let go of pad)
	// B) you're pressing a pad in sidebar (not a shortcut)
	// C) or you're not holding shift and ignore criteria above are met
	if (!on || x >= kDisplayWidth || (!Buttons::isShiftButtonPressed() && ignoreAction)) {
		return ActionResult::NOT_DEALT_WITH;
	}

	bool isUIPerformanceSessionView =
	    (getRootUI() == &performanceSessionView) || (getCurrentUI() == &performanceSessionView);

	if (on && (isUIModeWithinRange(shortcutPadUIModes) || isUIPerformanceSessionView)) {

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		const MenuItem* item = NULL;

		// performance session view
		if (isUIPerformanceSessionView) {
			if (x <= (kDisplayWidth - 2)) {
				item = paramShortcutsForSongView[x][y];
			}

			goto doSetup;
		}

		//For Kit Instrument Clip with Affect Entire Enabled
		else if (setupKitGlobalFXMenu && (getCurrentInstrumentType() == InstrumentType::KIT)
		         && (getCurrentInstrumentClip()->affectEntire)) {
			if (x <= (kDisplayWidth - 2)) {
				item = paramShortcutsForKitGlobalFX[x][y];
			}

			goto doSetup;
		}

		// AudioClips - there are just a few shortcuts
		else if (getCurrentClip()->type == CLIP_TYPE_AUDIO) {

			// NAME shortcut
			if (x == 11 && y == 5) {
				// Renames the output (track), not the clip
				Output* output = getCurrentOutput();
				if (output) {
					renameOutputUI.output = output;
					openUI(&renameOutputUI);
					return ActionResult::DEALT_WITH;
				}
			}
			else if (x <= 14) {
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
						display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_UNIMPLEMENTED));
						return ActionResult::DEALT_WITH;
					}

					if (display->haveOLED()) {
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
					}
					int32_t thingIndex = x & 1;

					bool setupSuccess = setup(getCurrentClip(), item, thingIndex);

					if (!setupSuccess) {
						return ActionResult::DEALT_WITH;
					}

					// If not in SoundEditor yet
					if (getCurrentUI() != &soundEditor) {
						if (getCurrentUI() == &sampleMarkerEditor) {
							display->setNextTransitionDirection(0);
							changeUIAtLevel(&soundEditor, 1);
							renderingNeededRegardlessOfUI(); // Not sure if this is 100% needed... some of it is.
						}
						else {
							openUI(&soundEditor);
						}
					}

					// Or if already in SoundEditor
					else {
						display->setNextTransitionDirection(0);
						beginScreen();
					}
				}
			}

			// Shortcut to patch a modulation source to the parameter we're already looking at
			else if (getCurrentUI() == &soundEditor) {

				PatchSource source = modSourceShortcuts[x - 14][y];
				if (source == PatchSource::SOON) {
					display->displayPopup("SOON");
				}

				if (source >= kLastPatchSource) {
					return ActionResult::DEALT_WITH;
				}

				bool previousPressStillActive = false;
				for (int32_t h = 0; h < 2; h++) {
					for (int32_t i = 0; i < kDisplayHeight; i++) {
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

				int32_t newNavigationDepth = navigationDepth;

				while (true) {

					// Ask current MenuItem what to do with this action
					MenuItem* newMenuItem = menuItemNavigationRecord[newNavigationDepth]->patchingSourceShortcutPress(
					    source, previousPressStillActive);

					// If it says "go up a level and ask that MenuItem", do that
					if (newMenuItem == (MenuItem*)0xFFFFFFFF) {
						newNavigationDepth--;
						if (newNavigationDepth < 0) { // This normally shouldn't happen
							exitCompletely();
							return ActionResult::DEALT_WITH;
						}
						wentBack = true;
					}

					// Otherwise...
					else {

						// If we've been given a MenuItem to go into, do that
						if (newMenuItem
						    && newMenuItem->checkPermissionToBeginSession(currentSound, currentSourceIndex,
						                                                  &currentMultiRange)
						           != MenuPermission::NO) {
							navigationDepth = newNavigationDepth + 1;
							menuItemNavigationRecord[navigationDepth] = newMenuItem;
							if (!wentBack) {
								display->setNextTransitionDirection(1);
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
	return ActionResult::DEALT_WITH;
}

extern uint16_t batteryMV;

ActionResult SoundEditor::padAction(int32_t x, int32_t y, int32_t on) {
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	bool isUIPerformanceSessionView =
	    (getRootUI() == &performanceSessionView) || (getCurrentUI() == &performanceSessionView);

	//used to convert column press to a shortcut to change Perform FX menu displayed
	if (isUIPerformanceSessionView && !Buttons::isShiftButtonPressed() && performanceSessionView.defaultEditingMode
	    && !performanceSessionView.editingParam) {
		if (x < kDisplayWidth) {
			performanceSessionView.padAction(x, y, on);
			return ActionResult::DEALT_WITH;
		}
	}

	if (!inSettingsMenu()) {
		ActionResult result = potentialShortcutPadAction(x, y, on);
		if (result != ActionResult::NOT_DEALT_WITH) {
			return result;
		}
	}

	if (getRootUI() == &keyboardScreen) {
		if (x < kDisplayWidth) {
			keyboardScreen.padAction(x, y, on);
			return ActionResult::DEALT_WITH;
		}
	}

	// Audition pads
	else if (getRootUI() == &instrumentClipView) {
		if (x == kDisplayWidth + 1) {
			instrumentClipView.padAction(x, y, on);
			return ActionResult::DEALT_WITH;
		}
	}

	else if (getRootUI() == &automationInstrumentClipView) {
		if (x == kDisplayWidth + 1) {
			automationInstrumentClipView.padAction(x, y, on);
			return ActionResult::DEALT_WITH;
		}
	}

	// Otherwise...
	if (currentUIMode == UI_MODE_NONE && on) {
		if (getCurrentMenuItem() == &firmwareVersionMenu && y == 7) {
			char buffer[12] = {0};

			// Read available internal memory
			if (x == 15) {
				auto& region = GeneralMemoryAllocator::get().regions[MEMORY_REGION_INTERNAL];
				intToString(region.end - region.start, buffer);
				display->displayPopup(buffer);
				return ActionResult::DEALT_WITH;
			}

			// Read active voices
			else if (x == 14) {
				intToString(AudioEngine::activeVoices.getNumElements(), buffer);
				display->displayPopup(buffer);
				return ActionResult::DEALT_WITH;
			}

			else if (x == 13) {
				intToString(picFirmwareVersion, buffer);
				display->displayPopup(buffer);
				return ActionResult::DEALT_WITH;
			}
		}

		//used in performanceSessionView to ignore pad presses when you just exited soundEditor
		//with a padAction
		if (getRootUI() == &performanceSessionView) {
			performanceSessionView.justExitedSoundEditor = true;
		}

		exitCompletely();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SoundEditor::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		return ActionResult::DEALT_WITH;
	}
	return getRootUI()->verticalEncoderAction(offset, inCardRoutine);
}

bool SoundEditor::pcReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t program) {
	if (currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()) {
		getCurrentMenuItem()->learnProgramChange(fromDevice, channel, program);
		return true;
	}
	return false;
}
bool SoundEditor::noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t note, int32_t velocity) {
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

void SoundEditor::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	// If learn button is pressed, learn this knob for current param
	if (currentUIMode == UI_MODE_MIDI_LEARN) {

		// But, can't do it if it's a Kit and affect-entire is on!
		if (editingKit() && getCurrentInstrumentClip()->affectEntire) {
			//IndicatorLEDs::indicateErrorOnLed(affectEntireLedX, affectEntireLedY);
		}

		// Otherwise, everything's fine
		else {
			getCurrentMenuItem()->learnKnob(NULL, whichModEncoder, getCurrentOutput()->modKnobMode, 255);
		}
	}

	// Otherwise, send the action to the Editor as usual
	else {
		UI::modEncoderAction(whichModEncoder, offset);
	}
}

bool SoundEditor::setup(Clip* clip, const MenuItem* item, int32_t sourceIndex) {

	Sound* newSound = NULL;
	ParamManagerForTimeline* newParamManager = NULL;
	ArpeggiatorSettings* newArpSettings = NULL;
	ModControllableAudio* newModControllable = NULL;

	bool isUISessionView =
	    (getRootUI() == &performanceSessionView) || (getRootUI() == &sessionView) || (getRootUI() == &arrangerView);

	//getParamManager and ModControllable for Performance Session View (and Session View)
	if (isUISessionView) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		if (modelStack) {
			newParamManager = (ParamManagerForTimeline*)modelStack->paramManager;
			newModControllable = (ModControllableAudio*)modelStack->modControllable;
		}
	}
	else if (clip) {

		// InstrumentClips
		if (clip->type == CLIP_TYPE_INSTRUMENT) {
			// Kit
			if (clip->output->type == InstrumentType::KIT) {
				Drum* selectedDrum = ((Kit*)clip->output)->selectedDrum;

				// If Affect Entire is selected
				if (setupKitGlobalFXMenu && ((InstrumentClip*)clip)->affectEntire) {
					newModControllable = (ModControllableAudio*)(Instrument*)clip->output->toModControllable();
					newParamManager = &(((InstrumentClip*)clip)->paramManager);
				}

				// If a SoundDrum is selected...
				else if (selectedDrum) {
					if (selectedDrum->type == DrumType::SOUND) {
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
							if (selectedDrum->type == DrumType::MIDI) {
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
						display->displayPopup(
						    deluge::l10n::get(deluge::l10n::String::STRING_FOR_SELECT_A_ROW_OR_AFFECT_ENTIRE));
					}
					return false;
				}
			}

			else {

				// Synth
				if (clip->output->type == InstrumentType::SYNTH) {
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
				if (getCurrentInstrumentType() == InstrumentType::MIDI_OUT) {
					soundEditorRootMenuMIDIOrCV.title = l10n::String::STRING_FOR_MIDI_INST_MENU_TITLE;
doMIDIOrCV:
					newItem = &soundEditorRootMenuMIDIOrCV;
				}
				else if (getCurrentInstrumentType() == InstrumentType::CV) {
					soundEditorRootMenuMIDIOrCV.title = l10n::String::STRING_FOR_CV_INSTRUMENT;
					goto doMIDIOrCV;
				}

				else if ((getCurrentInstrumentType() == InstrumentType::KIT)
				         && (getCurrentInstrumentClip()->affectEntire)) {
					newItem = &soundEditorRootMenuKitGlobalFX;
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
			if (getCurrentUI() == &automationInstrumentClipView) {
				display->cancelPopup();
				newItem = &deluge::gui::menu_item::runtime_feature::subMenuAutomation;
			}
			else if ((getCurrentUI() == &performanceSessionView) && !Buttons::isShiftButtonPressed()) {
				newItem = &soundEditorRootMenuPerformanceView;
			}
			else if (((getCurrentUI() == &sessionView) || (getCurrentUI() == &arrangerView))
			         && !Buttons::isShiftButtonPressed()) {
				newItem = &soundEditorRootMenuSongView;
			}
			else {
				newItem = &settingsRootMenu;
			}
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

	MenuPermission result = newItem->checkPermissionToBeginSession(newSound, sourceIndex, &newRange);

	if (result == MenuPermission::NO) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		return false;
	}
	else if (result == MenuPermission::MUST_SELECT_RANGE) {

		D_PRINTLN("must select range");

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

		if (result == MenuPermission::YES && currentMultiRange == NULL) {
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

	display->setNextTransitionDirection(1);
	return true;
}

MenuItem* SoundEditor::getCurrentMenuItem() {
	return menuItemNavigationRecord[navigationDepth];
}

bool SoundEditor::inSettingsMenu() {
	return (menuItemNavigationRecord[0] == &settingsRootMenu);
}

bool SoundEditor::inSongMenu() {
	return ((menuItemNavigationRecord[0] == &soundEditorRootMenuSongView)
	        || (menuItemNavigationRecord[0] == &soundEditorRootMenuPerformanceView));
}

bool SoundEditor::isUntransposedNoteWithinRange(int32_t noteCode) {
	return (soundEditor.currentSource->ranges.getNumElements() > 1
	        && soundEditor.currentSource->getRange(noteCode + soundEditor.currentSound->transpose)
	               == soundEditor.currentMultiRange);
}

void SoundEditor::setCurrentMultiRange(int32_t i) {
	currentMultiRangeIndex = i;
	currentMultiRange = (MultisampleRange*)soundEditor.currentSource->ranges.getElement(i);
}

MenuPermission SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam(Sound* sound, int32_t whichThing,
                                                                               bool automaticallySelectIfOnlyOne,
                                                                               ::MultiRange** previouslySelectedRange) {

	Source* source = &sound->sources[whichThing];

	::MultiRange* firstRange = source->getOrCreateFirstRange();
	if (!firstRange) {
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return MenuPermission::NO;
	}

	if (soundEditor.editingKit() || (automaticallySelectIfOnlyOne && source->ranges.getNumElements() == 1)) {
		*previouslySelectedRange = firstRange;
		return MenuPermission::YES;
	}

	if (getCurrentUI() == &soundEditor && *previouslySelectedRange && currentSourceIndex == whichThing) {
		return MenuPermission::YES;
	}

	return MenuPermission::MUST_SELECT_RANGE;
}

void SoundEditor::cutSound() {
	if (getCurrentClip()->type == CLIP_TYPE_AUDIO) {
		getCurrentAudioClip()->unassignVoiceSample();
	}
	else {
		soundEditor.currentSound->unassignAllVoices();
	}
}

AudioFileHolder* SoundEditor::getCurrentAudioFileHolder() {

	if (getCurrentClip()->type == CLIP_TYPE_AUDIO) {
		return &getCurrentAudioClip()->sampleHolder;
	}

	else {
		return currentMultiRange->getAudioFileHolder();
	}
}

ModelStackWithThreeMainThings* SoundEditor::getCurrentModelStack(void* memory) {
	bool isUISessionView =
	    (getRootUI() == &performanceSessionView) || (getRootUI() == &sessionView) || (getRootUI() == &arrangerView);

	InstrumentClip* clip = getCurrentInstrumentClip();
	Instrument* instrument = getCurrentInstrument();

	if (isUISessionView) {
		return currentSong->setupModelStackWithSongAsTimelineCounter(memory);
	}
	else if (instrument->type == InstrumentType::KIT && clip->affectEntire) {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(memory);

		return modelStack->addOtherTwoThingsButNoNoteRow(currentModControllable, currentParamManager);
	}
	else {
		NoteRow* noteRow = NULL;
		int32_t noteRowIndex;
		if (instrument->type == InstrumentType::KIT) {
			Drum* selectedDrum = ((Kit*)instrument)->selectedDrum;
			if (selectedDrum) {
				noteRow = clip->getNoteRowForDrum(selectedDrum, &noteRowIndex);
			}
		}

		return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, getCurrentClip(), noteRowIndex,
		                                                          noteRow, currentModControllable, currentParamManager);
	}
}

void SoundEditor::mpeZonesPotentiallyUpdated() {
	if (getCurrentUI() == this) {
		MenuItem* currentMenuItem = getCurrentMenuItem();
		if (currentMenuItem == &mpe::zoneNumMemberChannelsMenu) {
			currentMenuItem->readValueAgain();
		}
	}
}

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

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
*/
