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

#include "definitions.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "extern.h"
#include "gui/context_menu/context_menu_overwrite_bootloader.h"
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
#include "io/uart/uart.h"
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

<<<<<<< HEAD
class MenuItemPPQN : public MenuItemInteger {
public:
	MenuItemPPQN(char const* newName = NULL) : MenuItemInteger(newName) {}
	int getMinValue() { return 1; }
	int getMaxValue() { return 192; }
};

MenuItemSubmenu settingsRootMenu;

#if HAVE_OLED
char cvTransposeTitle[] = "CVx transpose";
char cvVoltsTitle[] = "CVx V/octave";
char gateModeTitle[] = "Gate outX mode";
char const* gateModeOptions[] = {"V-trig", "S-trig", NULL, NULL}; // Why'd I put two NULLs?
#else
char const* gateModeOptions[] = {"VTRI", "STRI", NULL, NULL};
#endif

// Gate stuff

class MenuItemGateMode final : public MenuItemSelection {
public:
	MenuItemGateMode()
	    : MenuItemSelection(
#if HAVE_OLED
	        gateModeTitle
#else
	        NULL
#endif
	    ) {
		basicOptions = gateModeOptions;
	}
	void readCurrentValue() {
		soundEditor.currentValue = cvEngine.gateChannels[soundEditor.currentSourceIndex].mode;
	}
	void writeCurrentValue() {
		cvEngine.setGateType(soundEditor.currentSourceIndex, soundEditor.currentValue);
	}
} gateModeMenu;

class MenuItemGateOffTime final : public MenuItemDecimal {
public:
	MenuItemGateOffTime(char const* newName = NULL) : MenuItemDecimal(newName) {}
	int getMinValue() { return 1; }
	int getMaxValue() { return 100; }
	int getNumDecimalPlaces() { return 1; }
	int getDefaultEditPos() { return 1; }
	void readCurrentValue() { soundEditor.currentValue = cvEngine.minGateOffTime; }
	void writeCurrentValue() { cvEngine.minGateOffTime = soundEditor.currentValue; }
} gateOffTimeMenu;

// Root menu

MenuItemSubmenu cvSubmenu;

class MenuItemCVVolts final : public MenuItemDecimal {
public:
	MenuItemCVVolts(char const* newName = NULL) : MenuItemDecimal(newName) {
#if HAVE_OLED
		basicTitle = cvVoltsTitle;
#endif
	}
	int getMinValue() {
		return 0;
	}
	int getMaxValue() {
		return 200;
	}
	int getNumDecimalPlaces() {
		return 2;
	}
	int getDefaultEditPos() {
		return 1;
	}
	void readCurrentValue() {
		soundEditor.currentValue = cvEngine.cvChannels[soundEditor.currentSourceIndex].voltsPerOctave;
	}
	void writeCurrentValue() {
		cvEngine.setCVVoltsPerOctave(soundEditor.currentSourceIndex, soundEditor.currentValue);
	}
#if HAVE_OLED
	void drawPixelsForOled() {
		if (soundEditor.currentValue == 0) {
			OLED::drawStringCentred("Hz/V", 20, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_HUGE_SPACING_X,
			                        TEXT_HUGE_SIZE_Y);
		}
		else MenuItemDecimal::drawPixelsForOled();
	}
#else
	void drawValue() {
		if (soundEditor.currentValue == 0) numericDriver.setText("HZPV", false, 255, true);
		else MenuItemDecimal::drawValue();
	}
#endif
	void horizontalEncoderAction(int offset) {
		if (soundEditor.currentValue != 0) MenuItemDecimal::horizontalEncoderAction(offset);
	}
} cvVoltsMenu;

class MenuItemCVTranspose final : public MenuItemDecimal {
public:
	MenuItemCVTranspose(char const* newName = NULL) : MenuItemDecimal(newName) {
#if HAVE_OLED
		basicTitle = cvTransposeTitle;
#endif
	}
	int getMinValue() {
		return -9600;
	}
	int getMaxValue() {
		return 9600;
	}
	int getNumDecimalPlaces() {
		return 2;
	}
	void readCurrentValue() {
		soundEditor.currentValue = (int32_t)cvEngine.cvChannels[soundEditor.currentSourceIndex].transpose * 100
		                           + cvEngine.cvChannels[soundEditor.currentSourceIndex].cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;
		cvEngine.setCVTranspose(soundEditor.currentSourceIndex, semitones - 256, cents);
	}
} cvTransposeMenu;

#if HAVE_OLED
static char const* cvOutputChannel[] = {"CV output 1", "CV output 2", NULL};
#else
static char const* cvOutputChannel[] = {"Out1", "Out2", NULL};
#endif

class MenuItemCVSelection final : public MenuItemSelection {
public:
	MenuItemCVSelection(char const* newName = NULL) : MenuItemSelection(newName) {
#if HAVE_OLED
		basicTitle = "CV outputs";
#endif
		basicOptions = cvOutputChannel;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) soundEditor.currentValue = 0;
		else soundEditor.currentValue = soundEditor.currentSourceIndex;
		MenuItemSelection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() {
		soundEditor.currentSourceIndex = soundEditor.currentValue;
#if HAVE_OLED
		cvSubmenu.basicTitle = cvOutputChannel[soundEditor.currentValue];
		cvVoltsTitle[2] = '1' + soundEditor.currentValue;
		cvTransposeTitle[2] = '1' + soundEditor.currentValue;
#endif
		return &cvSubmenu;
	}
} cvSelectionMenu;

class MenuItemGateSelection final : public MenuItemSelection {
public:
	MenuItemGateSelection(char const* newName = NULL) : MenuItemSelection(newName) {
#if HAVE_OLED
		basicTitle = "Gate outputs";
		static char const* options[] = {"Gate output 1", "Gate output 2",    "Gate output 3",
		                                "Gate output 4", "Minimum off-time", NULL};
#else
		static char const* options[] = {"Out1", "Out2", "Out3", "Out4", "OFFT", NULL};
#endif
		basicOptions = options;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) soundEditor.currentValue = 0;
		else soundEditor.currentValue = soundEditor.currentSourceIndex;
		MenuItemSelection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() {
		if (soundEditor.currentValue == NUM_GATE_CHANNELS) return &gateOffTimeMenu;
		else {
			soundEditor.currentSourceIndex = soundEditor.currentValue;
#if HAVE_OLED
			gateModeTitle[8] = '1' + soundEditor.currentValue;
#endif
			switch (soundEditor.currentValue) {
			case WHICH_GATE_OUTPUT_IS_CLOCK:
				gateModeOptions[2] = "Clock";
				break;

			case WHICH_GATE_OUTPUT_IS_RUN:
				gateModeOptions[2] = HAVE_OLED ? "\"Run\" signal" : "Run";
				break;

			default:
				gateModeOptions[2] = NULL;
				break;
			}
			return &gateModeMenu;
		}
	}
} gateSelectionMenu;

class MenuItemSwingInterval final : public MenuItemSyncLevel {
public:
	MenuItemSwingInterval(char const* newName = 0) : MenuItemSyncLevel(newName) {}
	void readCurrentValue() { soundEditor.currentValue = currentSong->swingInterval; }
	void writeCurrentValue() { currentSong->changeSwingInterval(soundEditor.currentValue); }

	void selectEncoderAction(int offset) { // So that there's no "off" option
		soundEditor.currentValue += offset;
		int numOptions = getNumOptions();

		// Wrap value
		if (soundEditor.currentValue >= numOptions) soundEditor.currentValue -= (numOptions - 1);
		else if (soundEditor.currentValue < 1) soundEditor.currentValue += (numOptions - 1);

		MenuItemValue::selectEncoderAction(offset);
	}
} swingIntervalMenu;

// Record submenu
MenuItemSubmenu recordSubmenu;

class MenuItemRecordQuantize final : public MenuItemSyncLevelRelativeToSong {
public:
	MenuItemRecordQuantize(char const* newName = 0) : MenuItemSyncLevelRelativeToSong(newName) {}
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::recordQuantizeLevel; }
	void writeCurrentValue() { FlashStorage::recordQuantizeLevel = soundEditor.currentValue; }
} recordQuantizeMenu;

class MenuItemRecordMargins final : public MenuItemSelection {
public:
	MenuItemRecordMargins(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::audioClipRecordMargins; }
	void writeCurrentValue() { FlashStorage::audioClipRecordMargins = soundEditor.currentValue; }
} recordMarginsMenu;

class MenuItemRecordCountIn final : public MenuItemSelection {
public:
	MenuItemRecordCountIn(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.countInEnabled; }
	void writeCurrentValue() { playbackHandler.countInEnabled = soundEditor.currentValue; }
} recordCountInMenu;

class MenuItemFlashStatus final : public MenuItemSelection {
public:
	MenuItemFlashStatus(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = PadLEDs::flashCursor; }
	void writeCurrentValue() {

		if (PadLEDs::flashCursor == FLASH_CURSOR_SLOW) PadLEDs::clearTickSquares();

		PadLEDs::flashCursor = soundEditor.currentValue;
	}
	char const** getOptions() {
		static char const* options[] = {"Fast", "Off", "Slow", NULL};
		return options;
	}
	int getNumOptions() { return 3; }
} flashStatusMenu;

class MenuItemMonitorMode final : public MenuItemSelection {
public:
	MenuItemMonitorMode(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = AudioEngine::inputMonitoringMode; }
	void writeCurrentValue() { AudioEngine::inputMonitoringMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Conditional", "On", "Off", NULL};
		return options;
	}
	int getNumOptions() { return NUM_INPUT_MONITORING_MODES; }
} monitorModeMenu;

class MenuItemSampleBrowserPreviewMode final : public MenuItemSelection {
public:
	MenuItemSampleBrowserPreviewMode(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::sampleBrowserPreviewMode; }
	void writeCurrentValue() { FlashStorage::sampleBrowserPreviewMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Off", "Conditional", "On", NULL};
		return options;
	}
	int getNumOptions() { return 3; }
} sampleBrowserPreviewModeMenu;

// Pads menu
MenuItemSubmenu padsSubmenu;

class MenuItemShortcutsVersion final : public MenuItemSelection {
public:
	MenuItemShortcutsVersion(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.shortcutsVersion; }
	void writeCurrentValue() { soundEditor.setShortcutsVersion(soundEditor.currentValue); }
	char const** getOptions() {
		static char const* options[] = {
#if HAVE_OLED
			"1.0",
			"3.0",
			NULL
#else
			"  1.0",
			"  3.0"
#endif
		};
		return options;
	}
	int getNumOptions() {
		return NUM_SHORTCUTS_VERSIONS;
	}
} shortcutsVersionMenu;

class MenuItemKeyboardLayout final : public MenuItemSelection {
public:
	MenuItemKeyboardLayout(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::keyboardLayout; }
	void writeCurrentValue() { FlashStorage::keyboardLayout = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {
			"QWERTY",
			"AZERTY",
#if HAVE_OLED
			"QWERTZ",
			NULL
#else
			"QRTZ"
#endif
		};
		return options;
	}
	int getNumOptions() {
		return NUM_KEYBOARD_LAYOUTS;
	}
} keyboardLayoutMenu;

// Colours submenu
MenuItemSubmenu coloursSubmenu;

MenuItemRuntimeFeatureSetting runtimeFeatureSettingMenuItem;
MenuItemRuntimeFeatureSettings runtimeFeatureSettingsMenu;

char const* firmwareString = "4.1.4-MUPADUW_TS_V4F";

// this class is haunted for some reason, clang-format mangles it
// clang-format off
class MenuItemFirmwareVersion final : public MenuItem {
public:
	MenuItemFirmwareVersion(char const* newName = 0) : MenuItem(newName){}

#if HAVE_OLED
	void drawPixelsForOled() {
		OLED::drawStringCentredShrinkIfNecessary(firmwareString, 22, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18, 20);
	}
#else
	void beginSession(MenuItem * navigatedBackwardFrom){
		drawValue();
	}

	void drawValue() {
		numericDriver.setScrollingText(firmwareString);
	}
#endif
} firmwareVersionMenu;
// clang-format on

// CV menu

// MIDI menu
MenuItemSubmenu midiMenu;

// MIDI clock menu
MenuItemSubmenu midiClockMenu;

// MIDI thru
class MenuItemMidiThru final : public MenuItemSelection {
public:
	MenuItemMidiThru(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = midiEngine.midiThru; }
	void writeCurrentValue() { midiEngine.midiThru = soundEditor.currentValue; }
} midiThruMenu;

// MIDI commands submenu
MenuItemSubmenu midiCommandsMenu;

MenuItemMidiCommand playbackRestartMidiCommand;
MenuItemMidiCommand playMidiCommand;
MenuItemMidiCommand recordMidiCommand;
MenuItemMidiCommand tapMidiCommand;
MenuItemMidiCommand undoMidiCommand;
MenuItemMidiCommand redoMidiCommand;
MenuItemMidiCommand loopMidiCommand;
MenuItemMidiCommand loopContinuousLayeringMidiCommand;

// MIDI device submenu - for after we've selected which device we want it for
MenuItemSubmenu midiDeviceMenu;

class MenuItemDefaultVelocityToLevel final : public MenuItemIntegerWithOff {
public:
	MenuItemDefaultVelocityToLevel(char const* newName = NULL) : MenuItemIntegerWithOff(newName) {}
	int getMaxValue() { return 50; }
	void readCurrentValue() {
		soundEditor.currentValue =
		    ((int64_t)soundEditor.currentMIDIDevice->defaultVelocityToLevel * 50 + 536870912) >> 30;
	}
	void writeCurrentValue() {
		soundEditor.currentMIDIDevice->defaultVelocityToLevel = soundEditor.currentValue * 21474836;
		currentSong->grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForEverything(soundEditor.currentMIDIDevice);
		MIDIDeviceManager::anyChangesToSave = true;
	}
} defaultVelocityToLevelMenu;

// Trigger clock menu
MenuItemSubmenu triggerClockMenu;

// Clock menu
MenuItemSubmenu triggerClockInMenu;
MenuItemSubmenu triggerClockOutMenu;

// MIDI input differentiation menu
class MenuItemMidiInputDifferentiation final : public MenuItemSelection {
public:
	MenuItemMidiInputDifferentiation(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = MIDIDeviceManager::differentiatingInputsByDevice; }
	void writeCurrentValue() { MIDIDeviceManager::differentiatingInputsByDevice = soundEditor.currentValue; }
} midiInputDifferentiationMenu;

class MenuItemMidiClockOutStatus final : public MenuItemSelection {
public:
	MenuItemMidiClockOutStatus(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.midiOutClockEnabled; }
	void writeCurrentValue() { playbackHandler.setMidiOutClockMode(soundEditor.currentValue); }
} midiClockOutStatusMenu;

class MenuItemMidiClockInStatus final : public MenuItemSelection {
public:
	MenuItemMidiClockInStatus(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.midiInClockEnabled; }
	void writeCurrentValue() { playbackHandler.setMidiInClockEnabled(soundEditor.currentValue); }
} midiClockInStatusMenu;

class MenuItemTempoMagnitudeMatching final : public MenuItemSelection {
public:
	MenuItemTempoMagnitudeMatching(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.tempoMagnitudeMatchingEnabled; }
	void writeCurrentValue() { playbackHandler.tempoMagnitudeMatchingEnabled = soundEditor.currentValue; }
} tempoMagnitudeMatchingMenu;

// Trigger clock in menu

class MenuItemTriggerInPPQN : public MenuItemPPQN {
public:
	MenuItemTriggerInPPQN(char const* newName = NULL) : MenuItemPPQN(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.analogInTicksPPQN; }
	void writeCurrentValue() {
		playbackHandler.analogInTicksPPQN = soundEditor.currentValue;
		if ((playbackHandler.playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) && playbackHandler.usingAnalogClockInput)
			playbackHandler.resyncInternalTicksToInputTicks(currentSong);
	}
} triggerInPPQNMenu;

class MenuItemTriggerInAutoStart final : public MenuItemSelection {
public:
	MenuItemTriggerInAutoStart(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.analogClockInputAutoStart; }
	void writeCurrentValue() { playbackHandler.analogClockInputAutoStart = soundEditor.currentValue; }
} triggerInAutoStartMenu;

// Trigger clock out menu

class MenuItemTriggerOutPPQN : public MenuItemPPQN {
public:
	MenuItemTriggerOutPPQN(char const* newName = NULL) : MenuItemPPQN(newName) {}
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.analogOutTicksPPQN; }
	void writeCurrentValue() {
		playbackHandler.analogOutTicksPPQN = soundEditor.currentValue;
		playbackHandler.resyncAnalogOutTicksToInternalTicks();
	}
} triggerOutPPQNMenu;

// Defaults menu
MenuItemSubmenu defaultsSubmenu;

MenuItemIntegerRange defaultTempoMenu;
MenuItemIntegerRange defaultSwingMenu;
MenuItemKeyRange defaultKeyMenu;

class MenuItemDefaultScale final : public MenuItemSelection {
public:
	MenuItemDefaultScale(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultScale; }
	void writeCurrentValue() { FlashStorage::defaultScale = soundEditor.currentValue; }
	int getNumOptions() { return NUM_PRESET_SCALES + 2; }
	char const** getOptions() { return presetScaleNames; }
} defaultScaleMenu;

class MenuItemDefaultVelocity final : public MenuItemInteger {
public:
	MenuItemDefaultVelocity(char const* newName = NULL) : MenuItemInteger(newName) {}
	int getMinValue() { return 1; }
	int getMaxValue() { return 127; }
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultVelocity; }
	void writeCurrentValue() {
		FlashStorage::defaultVelocity = soundEditor.currentValue;
		currentSong->setDefaultVelocityForAllInstruments(FlashStorage::defaultVelocity);
	}
} defaultVelocityMenu;

class MenuItemDefaultMagnitude final : public MenuItemSelection {
public:
	MenuItemDefaultMagnitude(char const* newName = NULL) : MenuItemSelection(newName) {}
	int getNumOptions() { return 7; }
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultMagnitude; }
	void writeCurrentValue() { FlashStorage::defaultMagnitude = soundEditor.currentValue; }
#if HAVE_OLED
	void drawPixelsForOled() {
		char buffer[12];
		intToString(96 << soundEditor.currentValue, buffer);
		OLED::drawStringCentred(buffer, 20 + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                        18, 20);
	}
#else
	void drawValue() {
		numericDriver.setTextAsNumber(96 << soundEditor.currentValue);
	}
#endif
} defaultMagnitudeMenu;

class MenuItemBendRangeDefault final : public MenuItemBendRange {
public:
	MenuItemBendRangeDefault(char const* newName = NULL) : MenuItemBendRange(newName) {}
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultBendRange[BEND_RANGE_MAIN]; }
	void writeCurrentValue() { FlashStorage::defaultBendRange[BEND_RANGE_MAIN] = soundEditor.currentValue; }
} defaultBendRangeMenu;

SoundEditor soundEditor{};
=======
SoundEditor soundEditor;
>>>>>>> community

SoundEditor::SoundEditor() {
	currentParamShorcutX = 255;
	memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
	timeLastAttemptedAutomatedParamEdit = 0;
	shouldGoUpOneLevelOnBegin = false;

#if HAVE_OLED
	init_menu_titles();
#endif
<<<<<<< HEAD

	// Sound editor menu -----------------------------------------------------------------------------

	// Modulator menu
	new (&modulatorTransposeMenu) MenuItemModulatorTranspose("Transpose", PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST);
	new (&modulatorVolume)
	    MenuItemSourceDependentPatchedParamFM(HAVE_OLED ? "Level" : "AMOUNT", PARAM_LOCAL_MODULATOR_0_VOLUME);
	new (&modulatorFeedbackMenu) MenuItemSourceDependentPatchedParamFM("FEEDBACK", PARAM_LOCAL_MODULATOR_0_FEEDBACK);
	new (&modulatorDestMenu) MenuItemModulatorDest("Destination");
	new (&modulatorPhaseMenu) MenuItemRetriggerPhase("Retrigger phase", true);
	static MenuItem* modulatorMenuItems[] = {&modulatorVolume,   &modulatorTransposeMenu, &modulatorFeedbackMenu,
	                                         &modulatorDestMenu, &modulatorPhaseMenu,     NULL};

	// Osc menu
	new (&oscTypeMenu) MenuItemOscType("TYPE");
	new (&sourceWaveIndexMenu) MenuItemSourceWaveIndex("Wave-index", PARAM_LOCAL_OSC_A_WAVE_INDEX);
	new (&sourceVolumeMenu) MenuItemSourceVolume(HAVE_OLED ? "Level" : "VOLUME", PARAM_LOCAL_OSC_A_VOLUME);
	new (&sourceFeedbackMenu) MenuItemSourceFeedback("FEEDBACK", PARAM_LOCAL_CARRIER_0_FEEDBACK);
	new (&fileSelectorMenu) MenuItemFileSelector("File browser");
	new (&audioRecorderMenu) MenuItemAudioRecorder("Record audio");
	new (&sampleReverseMenu) MenuItemSampleReverse("REVERSE");
	new (&sampleRepeatMenu) MenuItemSampleRepeat(HAVE_OLED ? "Repeat mode" : "MODE");
	new (&sampleStartMenu) MenuItemSampleStart("Start-point");
	new (&sampleEndMenu) MenuItemSampleEnd("End-point");
	new (&sourceTransposeMenu) MenuItemSourceTranspose("TRANSPOSE", PARAM_LOCAL_OSC_A_PITCH_ADJUST);
	new (&samplePitchSpeedMenu) MenuItemSamplePitchSpeed(HAVE_OLED ? "Pitch/speed" : "PISP");
	new (&timeStretchMenu) MenuItemTimeStretch("SPEED");
	new (&interpolationMenu) MenuItemInterpolation("INTERPOLATION");
	new (&pulseWidthMenu) MenuItemPulseWidth("PULSE WIDTH", PARAM_LOCAL_OSC_A_PHASE_WIDTH);
	new (&oscSyncMenu) MenuItemOscSync(HAVE_OLED ? "Oscillator sync" : "SYNC");
	new (&oscPhaseMenu) MenuItemRetriggerPhase("Retrigger phase", false);

	static MenuItem* oscMenuItems[] = {&oscTypeMenu,        &sourceVolumeMenu,    &sourceWaveIndexMenu,
	                                   &sourceFeedbackMenu, &fileSelectorMenu,    &audioRecorderMenu,
	                                   &sampleReverseMenu,  &sampleRepeatMenu,    &sampleStartMenu,
	                                   &sampleEndMenu,      &sourceTransposeMenu, &samplePitchSpeedMenu,
	                                   &timeStretchMenu,    &interpolationMenu,   &pulseWidthMenu,
	                                   &oscSyncMenu,        &oscPhaseMenu,        NULL};

	// LPF menu
	new (&lpfFreqMenu) MenuItemLPFFreq("Frequency", PARAM_LOCAL_LPF_FREQ);
	new (&lpfResMenu) MenuItemPatchedParamIntegerNonFM("Resonance", PARAM_LOCAL_LPF_RESONANCE);
	new (&lpfModeMenu) MenuItemLPFMode("MODE");
	static MenuItem* lpfMenuItems[] = {&lpfFreqMenu, &lpfResMenu, &lpfModeMenu, NULL};

	// HPF menu
	new (&hpfFreqMenu) MenuItemHPFFreq("Frequency", PARAM_LOCAL_HPF_FREQ);
	new (&hpfResMenu) MenuItemPatchedParamIntegerNonFM("Resonance", PARAM_LOCAL_HPF_RESONANCE);
	static MenuItem* hpfMenuItems[] = {&hpfFreqMenu, &hpfResMenu, NULL};

	// Envelope menu
	new (&envAttackMenu) MenuItemSourceDependentPatchedParam("ATTACK", PARAM_LOCAL_ENV_0_ATTACK);
	new (&envDecayMenu) MenuItemSourceDependentPatchedParam("DECAY", PARAM_LOCAL_ENV_0_DECAY);
	new (&envSustainMenu) MenuItemSourceDependentPatchedParam("SUSTAIN", PARAM_LOCAL_ENV_0_SUSTAIN);
	new (&envReleaseMenu) MenuItemSourceDependentPatchedParam("RELEASE", PARAM_LOCAL_ENV_0_RELEASE);
	static MenuItem* envMenuItems[] = {&envAttackMenu, &envDecayMenu, &envSustainMenu, &envReleaseMenu, NULL};

	// Unison menu
	new (&numUnisonMenu) MenuItemNumUnison(HAVE_OLED ? "Unison number" : "NUM");
	new (&unisonDetuneMenu) MenuItemUnisonDetune(HAVE_OLED ? "Unison detune" : "DETUNE");
	static MenuItem* unisonMenuItems[] = {&numUnisonMenu, &unisonDetuneMenu, NULL};

	// Arp menu
	new (&arpModeMenu) MenuItemArpMode("MODE");
	new (&arpSyncMenu) MenuItemArpSync("SYNC");
	new (&arpOctavesMenu) MenuItemArpOctaves(HAVE_OLED ? "Number of octaves" : "OCTAVES");
	new (&arpGateMenu) MenuItemArpGate("GATE", PARAM_UNPATCHED_SOUND_ARP_GATE);
	new (&arpGateMenuMIDIOrCV) MenuItemArpGateMIDIOrCV("GATE");
	new (&arpRateMenu) MenuItemArpRate("RATE", PARAM_GLOBAL_ARP_RATE);
	new (&arpRateMenuMIDIOrCV) MenuItemArpRateMIDIOrCV("RATE");
	static MenuItem* arpMenuItems[] = {&arpModeMenu,         &arpSyncMenu, &arpOctavesMenu,      &arpGateMenu,
	                                   &arpGateMenuMIDIOrCV, &arpRateMenu, &arpRateMenuMIDIOrCV, NULL};

	// Voice menu
	new (&polyphonyMenu) MenuItemPolyphony("POLYPHONY");
	new (&unisonMenu) MenuItemSubmenu("UNISON", unisonMenuItems);
	new (&portaMenu) MenuItemUnpatchedParam("PORTAMENTO", PARAM_UNPATCHED_SOUND_PORTA);
	new (&arpMenu) MenuItemArpeggiatorSubmenu("ARPEGGIATOR", arpMenuItems);
	new (&priorityMenu) MenuItemPriority("PRIORITY");
	static MenuItem* voiceMenuItems[] = {&polyphonyMenu, &unisonMenu, &portaMenu, &arpMenu, &priorityMenu, NULL};

	// LFO 1
	new (&lfo1TypeMenu) MenuItemLFO1Type(HAVE_OLED ? "SHAPE" : "TYPE");
	new (&lfo1RateMenu) MenuItemLFO1Rate("RATE", PARAM_GLOBAL_LFO_FREQ);
	new (&lfo1SyncMenu) MenuItemLFO1Sync("SYNC");
	static MenuItem* lfo1MenuItems[] = {&lfo1TypeMenu, &lfo1RateMenu, &lfo1SyncMenu, NULL};

	// LFO 2
	new (&lfo2TypeMenu) MenuItemLFO2Type(HAVE_OLED ? "SHAPE" : "TYPE");
	new (&lfo2RateMenu) MenuItemPatchedParamInteger("RATE", PARAM_LOCAL_LFO_LOCAL_FREQ);
	static MenuItem* lfo2MenuItems[] = {&lfo2TypeMenu, &lfo2RateMenu, NULL};

	// Mod FX menu
	new (&modFXTypeMenu) MenuItemModFXType("TYPE");
	new (&modFXRateMenu) MenuItemPatchedParamInteger("RATE", PARAM_GLOBAL_MOD_FX_RATE);
	new (&modFXFeedbackMenu) MenuItemModFXFeedback("FEEDBACK", PARAM_UNPATCHED_MOD_FX_FEEDBACK);
	new (&modFXDepthMenu) MenuItemModFXDepth("DEPTH", PARAM_GLOBAL_MOD_FX_DEPTH);
	new (&modFXOffsetMenu) MenuItemModFXOffset("OFFSET", PARAM_UNPATCHED_MOD_FX_OFFSET);
	static MenuItem* modFXMenuItems[] = {&modFXTypeMenu,  &modFXRateMenu,   &modFXFeedbackMenu,
	                                     &modFXDepthMenu, &modFXOffsetMenu, NULL};

	// EQ menu
	new (&bassMenu) MenuItemUnpatchedParam("BASS", PARAM_UNPATCHED_BASS);
	new (&trebleMenu) MenuItemUnpatchedParam("TREBLE", PARAM_UNPATCHED_TREBLE);
	new (&bassFreqMenu) MenuItemUnpatchedParam(HAVE_OLED ? "Bass frequency" : "BAFR", PARAM_UNPATCHED_BASS_FREQ);
	new (&trebleFreqMenu) MenuItemUnpatchedParam(HAVE_OLED ? "Treble frequency" : "TRFR", PARAM_UNPATCHED_TREBLE_FREQ);
	static MenuItem* eqMenuItems[] = {&bassMenu, &trebleMenu, &bassFreqMenu, &trebleFreqMenu, NULL};

	// Delay menu
	new (&delayFeedbackMenu) MenuItemPatchedParamInteger("AMOUNT", PARAM_GLOBAL_DELAY_FEEDBACK);
	new (&delayRateMenu) MenuItemPatchedParamInteger("RATE", PARAM_GLOBAL_DELAY_RATE);
	new (&delayPingPongMenu) MenuItemDelayPingPong("Pingpong");
	new (&delayAnalogMenu) MenuItemDelayAnalog("TYPE");
	new (&delaySyncMenu) MenuItemDelaySync("SYNC");
	static MenuItem* delayMenuItems[] = {&delayFeedbackMenu, &delayRateMenu, &delayPingPongMenu,
	                                     &delayAnalogMenu,   &delaySyncMenu, NULL};

	// Sidechain menu
	new (&sidechainSendMenu) MenuItemSidechainSend("Send to sidechain");
	new (&compressorVolumeShortcutMenu) MenuItemCompressorVolumeShortcut(
	    "Volume ducking", PARAM_GLOBAL_VOLUME_POST_REVERB_SEND, PATCH_SOURCE_COMPRESSOR);
	new (&reverbCompressorVolumeMenu) MenuItemReverbCompressorVolume("Volume ducking");
	new (&sidechainSyncMenu) MenuItemSidechainSync("SYNC");
	new (&compressorAttackMenu) MenuItemCompressorAttack("ATTACK");
	new (&compressorReleaseMenu) MenuItemCompressorRelease("RELEASE");
	new (&compressorShapeMenu) MenuItemUnpatchedParamUpdatingReverbParams("SHAPE", PARAM_UNPATCHED_COMPRESSOR_SHAPE);
	new (&reverbCompressorShapeMenu) MenuItemReverbCompressorShape("SHAPE");
	static MenuItem* sidechainMenuItemsForSound[] = {&sidechainSendMenu,
	                                                 &compressorVolumeShortcutMenu,
	                                                 &sidechainSyncMenu,
	                                                 &compressorAttackMenu,
	                                                 &compressorReleaseMenu,
	                                                 &compressorShapeMenu,
	                                                 NULL};
	static MenuItem* sidechainMenuItemsForReverb[] = {&reverbCompressorVolumeMenu, &sidechainSyncMenu,
	                                                  &compressorAttackMenu,       &compressorReleaseMenu,
	                                                  &reverbCompressorShapeMenu,  NULL};

	// Reverb menu
	new (&reverbAmountMenu) MenuItemPatchedParamInteger("AMOUNT", PARAM_GLOBAL_REVERB_AMOUNT);
	new (&reverbRoomSizeMenu) MenuItemReverbRoomSize(HAVE_OLED ? "Room size" : "SIZE");
	new (&reverbDampeningMenu) MenuItemReverbDampening("DAMPENING");
	new (&reverbWidthMenu) MenuItemReverbWidth("WIDTH");
	new (&reverbPanMenu) MenuItemReverbPan("PAN");
	new (&reverbCompressorMenu)
	    MenuItemCompressorSubmenu(HAVE_OLED ? "Reverb sidechain" : "SIDE", sidechainMenuItemsForReverb, true);
	static MenuItem* reverbMenuItems[] = {&reverbAmountMenu,
	                                      &reverbRoomSizeMenu,
	                                      &reverbDampeningMenu,
	                                      &reverbWidthMenu,
	                                      &reverbPanMenu,
	                                      &reverbCompressorMenu,
	                                      NULL};

	// FX menu
	new (&modFXMenu) MenuItemSubmenu(HAVE_OLED ? "Mod-fx" : "MODU", modFXMenuItems);
	new (&eqMenu) MenuItemSubmenu("EQ", eqMenuItems);
	new (&delayMenu) MenuItemSubmenu("DELAY", delayMenuItems);
	new (&reverbMenu) MenuItemSubmenu("REVERB", reverbMenuItems);
	new (&clippingMenu) MenuItemClipping("SATURATION");
	new (&srrMenu) MenuItemUnpatchedParam("DECIMATION", PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION);
	new (&bitcrushMenu) MenuItemUnpatchedParam(HAVE_OLED ? "Bitcrush" : "CRUSH", PARAM_UNPATCHED_BITCRUSHING);
	static MenuItem* fxMenuItems[] = {&modFXMenu,    &eqMenu,  &delayMenu,    &reverbMenu,
	                                  &clippingMenu, &srrMenu, &bitcrushMenu, NULL};

	// Bend ranges
	new (&mainBendRangeMenu) MenuItemBendRangeMain("Normal");
	new (&perFingerBendRangeMenu) MenuItemBendRangePerFinger(HAVE_OLED ? "Poly / finger / MPE" : "MPE");
	static MenuItem* bendMenuItems[] = {&mainBendRangeMenu, &perFingerBendRangeMenu, NULL};

	// Clip-level stuff
	new (&sequenceDirectionMenu) MenuItemSequenceDirection(HAVE_OLED ? "Play direction" : "DIRECTION");

	// Root menu
	new (&source0Menu) MenuItemActualSourceSubmenu(HAVE_OLED ? "Oscillator 1" : "OSC1", oscMenuItems, 0);
	new (&source1Menu) MenuItemActualSourceSubmenu(HAVE_OLED ? "Oscillator 2" : "OSC2", oscMenuItems, 1);
	new (&modulator0Menu) MenuItemModulatorSubmenu(HAVE_OLED ? "FM modulator 1" : "MOD1", modulatorMenuItems, 0);
	new (&modulator1Menu) MenuItemModulatorSubmenu(HAVE_OLED ? "FM modulator 2" : "MOD2", modulatorMenuItems, 1);
	new (&masterTransposeMenu) MenuItemMasterTranspose(HAVE_OLED ? "Master transpose" : "TRANSPOSE");
	new (&vibratoMenu) MenuItemFixedPatchCableStrength("VIBRATO", PARAM_LOCAL_PITCH_ADJUST, PATCH_SOURCE_LFO_GLOBAL);
	new (&noiseMenu) MenuItemPatchedParamIntegerNonFM(HAVE_OLED ? "Noise level" : "NOISE", PARAM_LOCAL_NOISE_VOLUME);
	new (&lpfMenu) MenuItemFilterSubmenu("LPF", lpfMenuItems);
	new (&hpfMenu) MenuItemFilterSubmenu("HPF", hpfMenuItems);
	new (&drumNameMenu) MenuItemDrumName("NAME");
	new (&synthModeMenu) MenuItemSynthMode(HAVE_OLED ? "Synth mode" : "MODE");
	new (&env0Menu) MenuItemEnvelopeSubmenu(HAVE_OLED ? "Envelope 1" : "ENV1", envMenuItems, 0);
	new (&env1Menu) MenuItemEnvelopeSubmenu(HAVE_OLED ? "Envelope 2" : "ENV2", envMenuItems, 1);
	new (&lfo0Menu) MenuItemSubmenu("LFO1", lfo1MenuItems);
	new (&lfo1Menu) MenuItemSubmenu("LFO2", lfo2MenuItems);
	new (&voiceMenu) MenuItemSubmenu("VOICE", voiceMenuItems);
	new (&fxMenu) MenuItemSubmenu("FX", fxMenuItems);
	new (&compressorMenu) MenuItemCompressorSubmenu("Sidechain compressor", sidechainMenuItemsForSound, false);
	new (&bendMenu) MenuItemBendSubmenu("Bend range", bendMenuItems);  // The submenu
	new (&drumBendRangeMenu) MenuItemBendRangePerFinger("Bend range"); // The single option available for Drums
	new (&volumeMenu) MenuItemPatchedParamInteger(HAVE_OLED ? "Level" : "VOLUME", PARAM_GLOBAL_VOLUME_POST_FX);
	new (&panMenu) MenuItemPatchedParamPan("PAN", PARAM_LOCAL_PAN);
	static MenuItem* soundEditorRootMenuItems[] = {&source0Menu,
	                                               &source1Menu,
	                                               &modulator0Menu,
	                                               &modulator1Menu,
	                                               &noiseMenu,
	                                               &masterTransposeMenu,
	                                               &vibratoMenu,
	                                               &lpfMenu,
	                                               &hpfMenu,
	                                               &drumNameMenu,
	                                               &synthModeMenu,
	                                               &env0Menu,
	                                               &env1Menu,
	                                               &lfo0Menu,
	                                               &lfo1Menu,
	                                               &voiceMenu,
	                                               &fxMenu,
	                                               &compressorMenu,
	                                               &bendMenu,
	                                               &drumBendRangeMenu,
	                                               &volumeMenu,
	                                               &panMenu,
	                                               &sequenceDirectionMenu,
	                                               NULL};

	new (&soundEditorRootMenu) MenuItemSubmenu("Sound", soundEditorRootMenuItems);

#if HAVE_OLED
	reverbAmountMenu.basicTitle = "Reverb amount";
	reverbWidthMenu.basicTitle = "Reverb width";
	reverbPanMenu.basicTitle = "Reverb pan";
	reverbCompressorMenu.basicTitle = "Reverb sidech.";

	sidechainSendMenu.basicTitle = "Send to sidech";
	sidechainSyncMenu.basicTitle = "Sidechain sync";
	compressorAttackMenu.basicTitle = "Sidech. attack";
	compressorReleaseMenu.basicTitle = "Sidech release";
	compressorShapeMenu.basicTitle = "Sidech. shape";
	reverbCompressorShapeMenu.basicTitle = "Sidech. shape";

	modFXTypeMenu.basicTitle = "MOD FX type";
	modFXRateMenu.basicTitle = "MOD FX rate";
	modFXFeedbackMenu.basicTitle = "MODFX feedback";
	modFXDepthMenu.basicTitle = "MOD FX depth";
	modFXOffsetMenu.basicTitle = "MOD FX offset";

	delayFeedbackMenu.basicTitle = "Delay amount";
	delayRateMenu.basicTitle = "Delay rate";
	delayPingPongMenu.basicTitle = "Delay pingpong";
	delayAnalogMenu.basicTitle = "Delay type";
	delaySyncMenu.basicTitle = "Delay sync";

	lfo1TypeMenu.basicTitle = "LFO1 type";
	lfo1RateMenu.basicTitle = "LFO1 rate";
	lfo1SyncMenu.basicTitle = "LFO1 sync";

	lfo2TypeMenu.basicTitle = "LFO2 type";
	lfo2RateMenu.basicTitle = "LFO2 rate";

	oscTypeMenu.basicTitle = oscTypeTitle;
	sourceVolumeMenu.basicTitle = oscLevelTitle;
	sourceWaveIndexMenu.basicTitle = waveIndexTitle;
	sourceFeedbackMenu.basicTitle = carrierFeedback;
	sampleReverseMenu.basicTitle = sampleReverseTitle;
	sampleRepeatMenu.basicTitle = sampleModeTitle;
	sourceTransposeMenu.basicTitle = oscTransposeTitle;
	timeStretchMenu.basicTitle = sampleSpeedTitle;
	interpolationMenu.basicTitle = sampleInterpolationTitle;
	pulseWidthMenu.basicTitle = pulseWidthTitle;
	oscPhaseMenu.basicTitle = retriggerPhaseTitle;

	modulatorTransposeMenu.basicTitle = modulatorTransposeTitle;
	modulatorDestMenu.basicTitle = "FM Mod2 dest.";
	modulatorVolume.basicTitle = modulatorLevelTitle;
	modulatorFeedbackMenu.basicTitle = modulatorFeedbackTitle;
	modulatorPhaseMenu.basicTitle = modulatorRetriggerPhaseTitle;

	lpfFreqMenu.basicTitle = "LPF frequency";
	lpfResMenu.basicTitle = "LPF resonance";
	lpfModeMenu.basicTitle = "LPF mode";

	hpfFreqMenu.basicTitle = "HPF frequency";
	hpfResMenu.basicTitle = "HPF resonance";

	envAttackMenu.basicTitle = attackTitle;
	envDecayMenu.basicTitle = decayTitle;
	envSustainMenu.basicTitle = sustainTitle;
	envReleaseMenu.basicTitle = releaseTitle;

	arpModeMenu.basicTitle = "Arp. mode";
	arpSyncMenu.basicTitle = "Arp. sync";
	arpOctavesMenu.basicTitle = "Arp. octaves";
	arpGateMenu.basicTitle = "Arp. gate";
	arpGateMenuMIDIOrCV.basicTitle = "Arp. gate";
	arpRateMenu.basicTitle = "Arp. rate";
	arpRateMenuMIDIOrCV.basicTitle = "Arp. rate";

	masterTransposeMenu.basicTitle = "Master tran.";
	compressorMenu.basicTitle = "Sidechain comp";
	volumeMenu.basicTitle = "Master level";
#endif

	// MIDIInstrument menu -------------------------------------------------

	new (&midiBankMenu) MenuItemMIDIBank("BANK");
	new (&midiSubMenu) MenuItemMIDISub(HAVE_OLED ? "Sub-bank" : "SUB");
	new (&midiPGMMenu) MenuItemMIDIPGM("PGM");

	// Root menu for MIDI / CV
	static MenuItem* soundEditorRootMenuItemsMIDIOrCV[] = {&midiPGMMenu, &midiBankMenu,          &midiSubMenu, &arpMenu,
	                                                       &bendMenu,    &sequenceDirectionMenu, NULL};

	new (&soundEditorRootMenuMIDIOrCV) MenuItemSubmenu("MIDI inst.", soundEditorRootMenuItemsMIDIOrCV);

#if HAVE_OLED
	midiBankMenu.basicTitle = "MIDI bank";
	midiSubMenu.basicTitle = "MIDI sub-bank";
	midiPGMMenu.basicTitle = "MIDI PGM numb.";
#endif

	// AudioClip menu system -------------------------------------------------------------------------------------------------------------------------------

	// Sample menu
	new (&audioClipModeMenu) MenuItemAudioClipMode("Stretch mode");
	new (&audioClipReverseMenu) MenuItemAudioClipReverse("REVERSE");
	new (&audioClipSampleMarkerEditorMenuStart) MenuItemAudioClipSampleMarkerEditor("", MARKER_START);
	new (&audioClipSampleMarkerEditorMenuEnd) MenuItemAudioClipSampleMarkerEditor("WAVEFORM", MARKER_END);
	static MenuItem* audioClipSampleMenuItems[] = {&fileSelectorMenu,
	                                               &audioClipReverseMenu,
	                                               &audioClipModeMenu,
	                                               &samplePitchSpeedMenu,
	                                               &audioClipSampleMarkerEditorMenuEnd,
	                                               &interpolationMenu,
	                                               NULL};

	// LPF menu
	new (&audioClipLPFFreqMenu) MenuItemAudioClipLPFFreq("Frequency", PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ);
	new (&audioClipLPFResMenu) MenuItemUnpatchedParam("Resonance", PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES);
	static MenuItem* audioClipLPFMenuItems[] = {&audioClipLPFFreqMenu, &audioClipLPFResMenu, &lpfModeMenu, NULL};

	// HPF menu
	new (&audioClipHPFFreqMenu) MenuItemAudioClipHPFFreq("Frequency", PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ);
	new (&audioClipHPFResMenu) MenuItemUnpatchedParam("Resonance", PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES);
	static MenuItem* audioClipHPFMenuItems[] = {&audioClipHPFFreqMenu, &audioClipHPFResMenu, NULL};

	// Mod FX menu
	new (&audioClipModFXTypeMenu) MenuItemAudioClipModFXType("TYPE");
	new (&audioClipModFXRateMenu) MenuItemUnpatchedParam("RATE", PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE);
	new (&audioClipModFXDepthMenu) MenuItemUnpatchedParam("DEPTH", PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH);
	static MenuItem* audioClipModFXMenuItems[] = {&audioClipModFXTypeMenu,  &audioClipModFXRateMenu, &modFXFeedbackMenu,
	                                              &audioClipModFXDepthMenu, &modFXOffsetMenu,        NULL};

	// Delay menu
	new (&audioClipDelayFeedbackMenu) MenuItemUnpatchedParam("AMOUNT", PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT);
	new (&audioClipDelayRateMenu) MenuItemUnpatchedParam("RATE", PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE);
	static MenuItem* audioClipDelayMenuItems[] = {&audioClipDelayFeedbackMenu,
	                                              &audioClipDelayRateMenu,
	                                              &delayPingPongMenu,
	                                              &delayAnalogMenu,
	                                              &delaySyncMenu,
	                                              NULL};

	// Reverb menu
	new (&audioClipReverbSendAmountMenu)
	    MenuItemUnpatchedParam("AMOUNT", PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT);
	static MenuItem* audioClipReverbMenuItems[] = {&audioClipReverbSendAmountMenu,
	                                               &reverbRoomSizeMenu,
	                                               &reverbDampeningMenu,
	                                               &reverbWidthMenu,
	                                               &reverbPanMenu,
	                                               &reverbCompressorMenu,
	                                               NULL};

	// FX menu
	new (&audioClipModFXMenu) MenuItemSubmenu(HAVE_OLED ? "Mod-fx" : "MODU", audioClipModFXMenuItems);
	new (&audioClipDelayMenu) MenuItemSubmenu("DELAY", audioClipDelayMenuItems);
	new (&audioClipReverbMenu) MenuItemSubmenu("REVERB", audioClipReverbMenuItems);
	static MenuItem* audioClipFXMenuItems[] = {&audioClipModFXMenu, &eqMenu,  &audioClipDelayMenu, &audioClipReverbMenu,
	                                           &clippingMenu,       &srrMenu, &bitcrushMenu,       NULL};

	// Sidechain menu
	new (&audioClipCompressorVolumeMenu)
	    MenuItemUnpatchedParamUpdatingReverbParams("Volume ducking", PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME);
	static MenuItem* audioClipSidechainMenuItems[] = {&audioClipCompressorVolumeMenu, &sidechainSyncMenu,
	                                                  &compressorAttackMenu,          &compressorReleaseMenu,
	                                                  &compressorShapeMenu,           NULL};

	// Root menu for AudioClips
	new (&audioClipSampleMenu) MenuItemSubmenu("SAMPLE", audioClipSampleMenuItems);
	new (&audioClipTransposeMenu) MenuItemAudioClipTranspose("TRANSPOSE");
	new (&audioClipLPFMenu) MenuItemSubmenu("LPF", audioClipLPFMenuItems);
	new (&audioClipHPFMenu) MenuItemSubmenu("HPF", audioClipHPFMenuItems);
	new (&audioClipAttackMenu) MenuItemAudioClipAttack("ATTACK");
	new (&audioClipFXMenu) MenuItemSubmenu("FX", audioClipFXMenuItems);
	new (&audioClipCompressorMenu) MenuItemSubmenu("Sidechain compressor", audioClipSidechainMenuItems);
	new (&audioClipLevelMenu)
	    MenuItemUnpatchedParam(HAVE_OLED ? "Level" : "VOLUME", PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME);
	new (&audioClipPanMenu) MenuItemUnpatchedParamPan("PAN", PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN);

	static MenuItem* soundEditorRootMenuItemsAudioClip[] = {&audioClipSampleMenu,
	                                                        &audioClipTransposeMenu,
	                                                        &audioClipLPFMenu,
	                                                        &audioClipHPFMenu,
	                                                        &audioClipAttackMenu,
	                                                        &priorityMenu,
	                                                        &audioClipFXMenu,
	                                                        &audioClipCompressorMenu,
	                                                        &audioClipLevelMenu,
	                                                        &audioClipPanMenu,
	                                                        NULL};

	new (&soundEditorRootMenuAudioClip) MenuItemSubmenu("Audio clip", soundEditorRootMenuItemsAudioClip);

#if HAVE_OLED
	audioClipReverbSendAmountMenu.basicTitle = "Reverb amount";

	audioClipDelayFeedbackMenu.basicTitle = "Delay amount";
	audioClipDelayRateMenu.basicTitle = "Delay rate";

	audioClipModFXTypeMenu.basicTitle = "MOD FX type";
	audioClipModFXRateMenu.basicTitle = "MOD FX rate";
	audioClipModFXDepthMenu.basicTitle = "MOD FX depth";

	audioClipLPFFreqMenu.basicTitle = "LPF frequency";
	audioClipLPFResMenu.basicTitle = "LPF resonance";

	audioClipHPFFreqMenu.basicTitle = "HPF frequency";
	audioClipHPFResMenu.basicTitle = "HPF resonance";

#endif

#if ALPHA_OR_BETA_VERSION && IN_HARDWARE_DEBUG
	// Dev vars.......
	new (&devVarAMenu) DevVarAMenu();
	new (&devVarBMenu) DevVarBMenu();
	new (&devVarCMenu) DevVarCMenu();
	new (&devVarDMenu) DevVarDMenu();
	new (&devVarEMenu) DevVarEMenu();
	new (&devVarFMenu) DevVarFMenu();
	new (&devVarGMenu) DevVarGMenu();
#endif

	// Patching stuff
	new (&sourceSelectionMenuRegular) MenuItemSourceSelectionRegular();
	new (&sourceSelectionMenuRange) MenuItemSourceSelectionRange();
	new (&patchCableStrengthMenuRegular) MenuItemPatchCableStrengthRegular();
	new (&patchCableStrengthMenuRange) MenuItemPatchCableStrengthRange();

	new (&multiRangeMenu) MenuItemMultiRange();
=======
>>>>>>> community
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
	IndicatorLEDs::setLedState(saveLedX, saveLedY, false); // In case we came from the save-Instrument UI

	IndicatorLEDs::setLedState(synthLedX, synthLedY, !inSettingsMenu() && !editingKit() && currentSound);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, !inSettingsMenu() && editingKit() && currentSound);
	IndicatorLEDs::setLedState(midiLedX, midiLedY,
	                           !inSettingsMenu() && currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT);
	IndicatorLEDs::setLedState(cvLedX, cvLedY,
	                           !inSettingsMenu() && currentSong->currentClip->output->type == INSTRUMENT_TYPE_CV);

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, false);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);

	IndicatorLEDs::blinkLed(backLedX, backLedY);

	playbackHandler.setLedStates();
}

int SoundEditor::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Encoder button
	if (x == selectEncButtonX && y == selectEncButtonY) {
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
	else if (x == backButtonX && y == backButtonY) {
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
	else if (x == saveButtonX && y == saveButtonY) {
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
	else if (x == learnButtonX && y == learnButtonY) {
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
						IndicatorLEDs::blinkLed(learnLedX, learnLedY, 255, 1);
						currentUIMode = UI_MODE_MIDI_LEARN;
					}
				}
			}
		}
		else {
			if (getCurrentMenuItem()->shouldBlinkLearnLed()) {
				IndicatorLEDs::blinkLed(learnLedX, learnLedY);
			}
			else {
				IndicatorLEDs::setLedState(learnLedX, learnLedY, false);
			}

			if (currentUIMode == UI_MODE_MIDI_LEARN) {
				currentUIMode = UI_MODE_NONE;
			}
		}
	}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	else if (x == clipViewButtonX && y == clipViewButtonY && getRootUI() == &instrumentClipView) {
		return instrumentClipView.buttonAction(x, y, on, inCardRoutine);
	}
#else

	// Affect-entire button
	else if (x == affectEntireButtonX && y == affectEntireButtonY && getRootUI() == &instrumentClipView) {
		if (getCurrentMenuItem()->usesAffectEntire() && editingKit()) {
			if (inCardRoutine) {
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (on) {
				if (currentUIMode == UI_MODE_NONE) {
					IndicatorLEDs::blinkLed(affectEntireLedX, affectEntireLedY, 255, 1);
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
			return instrumentClipView.InstrumentClipMinder::buttonAction(x, y, on, inCardRoutine);
		}
	}

	// Keyboard button
	else if (x == keyboardButtonX && y == keyboardButtonY) {
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

			IndicatorLEDs::setLedState(keyboardLedX, keyboardLedY, getRootUI() == &keyboardScreen);
		}
	}
#endif

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

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD

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
#endif

shortcutsPicked:

	if (currentItem->shouldBlinkLearnLed()) {
		IndicatorLEDs::blinkLed(learnLedX, learnLedY);
	}
	else {
		IndicatorLEDs::setLedState(learnLedX, learnLedY, false);
	}

	possibleChangeToCurrentRangeDisplay();

	return true;
}

void SoundEditor::possibleChangeToCurrentRangeDisplay() {
	uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
	uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
}

void SoundEditor::setupShortcutBlink(int x, int y, int frequency) {
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	currentParamShorcutX = x;
	currentParamShorcutY = y;

	shortcutBlinkCounter = 0;
	paramShortcutBlinkFrequency = frequency;
#endif
}

void SoundEditor::setupExclusiveShortcutBlink(int x, int y) {
	memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
	setupShortcutBlink(x, y, 1);
	blinkShortcut();
}

void SoundEditor::blinkShortcut() {

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	return;
#endif

	// We have to blink params and shortcuts at slightly different times, because blinking two pads on the same row at same time doesn't work

	uint32_t counterForNow = shortcutBlinkCounter >> 1;

	if (shortcutBlinkCounter & 1) {
		// Blink param
		if ((counterForNow & paramShortcutBlinkFrequency) == 0) {
			bufferPICPadsUart(24 + currentParamShorcutY + (currentParamShorcutX * displayHeight));
		}
		uiTimerManager.setTimer(TIMER_SHORTCUT_BLINK, 180);
	}

	else {
		// Blink source
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < displayHeight; y++) {
				if (sourceShortcutBlinkFrequencies[x][y] != 255
				    && (counterForNow & sourceShortcutBlinkFrequencies[x][y]) == 0) {
					if (sourceShortcutBlinkColours[x][y]) {
						bufferPICPadsUart(10 + sourceShortcutBlinkColours[x][y]);
					}
					bufferPICPadsUart(24 + y + ((x + 14) * displayHeight));
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

	if (!on || DELUGE_MODEL == DELUGE_MODEL_40_PAD || x >= displayWidth
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
				bool available = contextMenuOverwriteBootloader.setupAndCheckAvailability();
				if (available) {
					openUI(&contextMenuOverwriteBootloader);
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
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(xEncButtonX, xEncButtonY)) {
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
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
			IndicatorLEDs::indicateAlertOnLed(
			    songViewLedX, songViewLedY); // Really should indicate it on "Clip View", but that's already blinking
#else
			//IndicatorLEDs::indicateErrorOnLed(affectEntireLedX, affectEntireLedY);
#endif
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
								IndicatorLEDs::indicateAlertOnLed(midiLedX, midiLedY);
							}
							else { // GATE
								IndicatorLEDs::indicateAlertOnLed(cvLedX, cvLedY);
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

		Uart::println("must select range");

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
