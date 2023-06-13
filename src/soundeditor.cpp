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

#include <AudioEngine.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <MenuItemMultiRange.h>
#include <samplebrowser.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include "soundeditor.h"
#include "matrixdriver.h"
#include "storagemanager.h"
#include "functions.h"
#include "saveinstrumentpresetui.h"
#include "definitions.h"
#include "numericdriver.h"
#include "uart.h"
#include "KeyboardScreen.h"
#include "AudioRecorder.h"
#include "source.h"
#include "kit.h"
#include "View.h"
#include "MenuItemSubmenu.h"
#include "MenuItemPatchedParam.h"
#include "MenuItemUnpatchedParam.h"
#include "MenuItemSelection.h"
#include "MenuItemDecimal.h"
#include "MenuItemSyncLevel.h"
#include "MenuItemSourceSelection.h"
#include "MenuItemPatchCableStrength.h"
#include "MenuItemMidiCommand.h"
#include "CVEngine.h"
#include "midiengine.h"
#include <string.h>
#include "PlaybackMode.h"
#include "ActionLogger.h"
#include "MultisampleRange.h"
#include "MenuItemSampleLoopPoint.h"
#include "SampleMarkerEditor.h"
#include "MenuItemFileSelector.h"
#include <new>
#include "MenuItemIntegerRange.h"
#include "MenuItemKeyRange.h"
#include "MenuItemDrumName.h"
#include "RenameDrumUI.h"
#include "song.h"
#include "NoteRow.h"
#include "AudioClip.h"
#include "AudioClipView.h"
#include "MenuItemAudioClipSampleMarkerEditor.h"
#include "MenuItemColour.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "FlashStorage.h"
#include "Buttons.h"
#include "revmodel.hpp"
#include "ModelStack.h"
#include "extern.h"
#include "MultiWaveTableRange.h"
#include "MenuItemMIDIDevices.h"
#include "MenuItemMPEDirectionSelector.h"
#include "MenuItemMPEZoneNumMemberChannels.h"
#include "MenuItemMPEZoneSelector.h"
#include "uitimermanager.h"
#include "ParamSet.h"
#include "PatchCableSet.h"
#include "MIDIDevice.h"
#include "ContextMenuOverwriteBootloader.h"

#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "sio_char.h"
#include "cfunctions.h"
}

SoundEditor soundEditor;

MenuItemSubmenu soundEditorRootMenu;
MenuItemSubmenu soundEditorRootMenuMIDIOrCV;
MenuItemSubmenu soundEditorRootMenuAudioClip;

// Dev vars

class DevVarAMenu final : public MenuItemInteger {
public:
	DevVarAMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarA; }
	void writeCurrentValue() { storageManager.devVarA = soundEditor.currentValue; }
	int getMaxValue() { return 512; }
} devVarAMenu;

class DevVarBMenu final : public MenuItemInteger {
public:
	DevVarBMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarB; }
	void writeCurrentValue() { storageManager.devVarB = soundEditor.currentValue; }
	int getMaxValue() { return 512; }
} devVarBMenu;

class DevVarCMenu final : public MenuItemInteger {
public:
	DevVarCMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarC; }
	void writeCurrentValue() { storageManager.devVarC = soundEditor.currentValue; }
	int getMaxValue() { return 1024; }
} devVarCMenu;

class DevVarDMenu final : public MenuItemInteger {
public:
	DevVarDMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarD; }
	void writeCurrentValue() { storageManager.devVarD = soundEditor.currentValue; }
	int getMaxValue() { return 1024; }
} devVarDMenu;

class DevVarEMenu final : public MenuItemInteger {
public:
	DevVarEMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarE; }
	void writeCurrentValue() { storageManager.devVarE = soundEditor.currentValue; }
	int getMaxValue() { return 1024; }
} devVarEMenu;

class DevVarFMenu final : public MenuItemInteger {
public:
	DevVarFMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarF; }
	void writeCurrentValue() { storageManager.devVarF = soundEditor.currentValue; }
	int getMaxValue() { return 1024; }
} devVarFMenu;

class DevVarGMenu final : public MenuItemInteger {
public:
	DevVarGMenu(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = storageManager.devVarG; }
	void writeCurrentValue() { storageManager.devVarG = soundEditor.currentValue; }
	int getMaxValue() { return 1024; }
	int getMinValue() { return -1024; }
} devVarGMenu;

#if HAVE_OLED
char oscTypeTitle[] = "OscX type";
char oscLevelTitle[] = "OscX level";
char waveIndexTitle[] = "OscX wave-ind.";
char carrierFeedback[] = "CarrierX feed.";
char sampleReverseTitle[] = "SampX reverse";
char sampleModeTitle[] = "SampX repeat";
char oscTransposeTitle[] = "OscX transpose";
char sampleSpeedTitle[] = "SampX speed";
char sampleInterpolationTitle[] = "SampX interp.";
char pulseWidthTitle[] = "OscX p. width";
char retriggerPhaseTitle[] = "OscX r. phase";

char attackTitle[] = "EnvX attack";
char decayTitle[] = "EnvX decay";
char sustainTitle[] = "EnvX sustain";
char releaseTitle[] = "EnvX release";

char modulatorTransposeTitle[] = "FM ModX tran.";
char modulatorLevelTitle[] = "FM ModX level";
char modulatorFeedbackTitle[] = "FM ModX f.back";
char modulatorRetriggerPhaseTitle[] = "FM ModX retrig";

void setOscillatorNumberForTitles(int s) {
	oscTypeTitle[3] = '1' + s;
	oscLevelTitle[3] = '1' + s;
	waveIndexTitle[3] = '1' + s;
	oscTransposeTitle[3] = '1' + s;
	pulseWidthTitle[3] = '1' + s;
	retriggerPhaseTitle[3] = '1' + s;

	carrierFeedback[7] = '1' + s;

	sampleReverseTitle[4] = '1' + s;
	sampleModeTitle[4] = '1' + s;
	sampleSpeedTitle[4] = '1' + s;
	sampleInterpolationTitle[4] = '1' + s;
}

void setEnvelopeNumberForTitles(int e) {
	attackTitle[3] = '1' + e;
	decayTitle[3] = '1' + e;
	sustainTitle[3] = '1' + e;
	releaseTitle[3] = '1' + e;
}

void setModulatorNumberForTitles(int m) {
	modulatorTransposeTitle[6] = '1' + m;
	modulatorLevelTitle[6] = '1' + m;
	modulatorFeedbackTitle[6] = '1' + m;
	modulatorRetriggerPhaseTitle[6] = '1' + m;
}
#endif

class MenuItemModulatorSubmenu final : public MenuItemSubmenuReferringToOneThing {
public:
	MenuItemModulatorSubmenu(char const* newName = NULL, MenuItem** newFirstItem = NULL, int newSourceIndex = 0)
	    : MenuItemSubmenuReferringToOneThing(newName, newFirstItem, newSourceIndex) {}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setModulatorNumberForTitles(thingIndex);
		MenuItemSubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
	}
#endif
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->synthMode == SYNTH_MODE_FM);
	}
};

class MenuItemFilterSubmenu final : public MenuItemSubmenu {
public:
	MenuItemFilterSubmenu(char const* newName = NULL, MenuItem** newFirstItem = NULL)
	    : MenuItemSubmenu(newName, newFirstItem) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->synthMode != SYNTH_MODE_FM); }
};

class MenuItemSelectionSample : public MenuItemSelection {
public:
	MenuItemSelectionSample(char const* newName = NULL) : MenuItemSelection(newName) {}
	bool isRelevant(Sound* sound, int whichThing) {
		if (!sound) return true; // For AudioClips
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE
		        && source->hasAtLeastOneAudioFileLoaded());
	}
};

class MenuItemLFOShape : public MenuItemSelection {
public:
	MenuItemLFOShape(char const* newName = NULL) : MenuItemSelection(newName) {}
	char const** getOptions() {
		static char const* options[] = {"Sine", "Triangle", "Square", "Saw", "S&H", "Random Walk", NULL};
		return options;
	}
	int getNumOptions() { return NUM_LFO_TYPES; }
};

// Root menu ----------------------------------------------------------------------------------------------------

class MenuItemActualSourceSubmenu final : public MenuItemSubmenuReferringToOneThing {
public:
	MenuItemActualSourceSubmenu(char const* newName = 0, MenuItem** newItems = 0, int newSourceIndex = 0)
	    : MenuItemSubmenuReferringToOneThing(newName, newItems, newSourceIndex) {}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setOscillatorNumberForTitles(thingIndex);
		MenuItemSubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
	}
#else
	void drawName() {
		if (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_FM) {
			char buffer[5];
			strcpy(buffer, "CAR");
			intToString(thingIndex + 1, buffer + 3);
			numericDriver.setText(buffer);
		}
		else MenuItemSubmenuReferringToOneThing::drawName();
	}
#endif
};

MenuItemActualSourceSubmenu source0Menu;
MenuItemActualSourceSubmenu source1Menu;
MenuItemModulatorSubmenu modulator0Menu;
MenuItemModulatorSubmenu modulator1Menu;

class MenuItemMasterTranspose final : public MenuItemInteger, public MenuItemPatchedParam {
public:
	MenuItemMasterTranspose(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->transpose; }
	void writeCurrentValue() {
		soundEditor.currentSound->transpose = soundEditor.currentValue;
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}
	MenuItem* selectButtonPress() { return MenuItemPatchedParam::selectButtonPress(); }
	uint8_t shouldDrawDotOnName() { return MenuItemPatchedParam::shouldDrawDotOnName(); }
	uint8_t getPatchedParamIndex() { return PARAM_LOCAL_PITCH_ADJUST; }
	uint8_t getP() { return PARAM_LOCAL_PITCH_ADJUST; }
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {
		return MenuItemPatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) {
		return MenuItemPatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}
#if !HAVE_OLED
	void drawValue() {
		MenuItemPatchedParam::drawValue();
	}
#endif

	void unlearnAction() {
		MenuItemWithCCLearning::unlearnAction();
	}
	bool allowsLearnMode() {
		return MenuItemWithCCLearning::allowsLearnMode();
	}
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	int getMinValue() {
		return -96;
	}
	int getMaxValue() {
		return 96;
	}
} masterTransposeMenu;

class MenuItemPatchedParamIntegerNonFM : public MenuItemPatchedParamInteger {
public:
	MenuItemPatchedParamIntegerNonFM(char const* newName = 0, int newP = 0)
	    : MenuItemPatchedParamInteger(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->synthMode != SYNTH_MODE_FM); }
};

MenuItemPatchedParamIntegerNonFM noiseMenu;

MenuItemFilterSubmenu lpfMenu;
MenuItemFilterSubmenu hpfMenu;

MenuItemDrumName drumNameMenu;

class MenuItemSynthMode final : public MenuItemSelection {
public:
	MenuItemSynthMode(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->synthMode; }
	void writeCurrentValue() {
		soundEditor.currentSound->setSynthMode(soundEditor.currentValue, currentSong);
		view.setKnobIndicatorLevels();
	}
	char const** getOptions() {
		static char const* options[] = {"Subtractive", "FM", "Ringmod", NULL};
		return options;
	}
	int getNumOptions() { return 3; }
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->sources[0].oscType < NUM_OSC_TYPES_RINGMODDABLE
		        && sound->sources[1].oscType < NUM_OSC_TYPES_RINGMODDABLE);
	}
} synthModeMenu;

class MenuItemBendSubmenu final : public MenuItemSubmenu {
public:
	MenuItemBendSubmenu(char const* newName = NULL, MenuItem** newItems = NULL) : MenuItemSubmenu(newName, newItems) {}
	bool isRelevant(Sound* sound, int whichThing) {
		return (
		    currentSong->currentClip->output->type == INSTRUMENT_TYPE_SYNTH
		    || currentSong->currentClip->output->type
		           == INSTRUMENT_TYPE_CV); // Drums within a Kit don't need the two-item submenu - they have their own single item.
	}
} bendMenu;

class MenuItemEnvelopeSubmenu final : public MenuItemSubmenuReferringToOneThing {
public:
	MenuItemEnvelopeSubmenu() {}
	MenuItemEnvelopeSubmenu(char const* newName, MenuItem** newItems, int newSourceIndex)
	    : MenuItemSubmenuReferringToOneThing(newName, newItems, newSourceIndex) {}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom = NULL) {
		MenuItemSubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
		setEnvelopeNumberForTitles(thingIndex);
	}
#endif
};

MenuItemEnvelopeSubmenu env0Menu;
MenuItemEnvelopeSubmenu env1Menu;

MenuItemSubmenu lfo0Menu;
MenuItemSubmenu lfo1Menu;

MenuItemSubmenu voiceMenu;
MenuItemSubmenu fxMenu;
MenuItemCompressorSubmenu compressorMenu;

MenuItemPatchedParamInteger volumeMenu;
MenuItemPatchedParamPan panMenu;

// LPF menu ----------------------------------------------------------------------------------------------------

class MenuItemLPFFreq final : public MenuItemPatchedParamIntegerNonFM{
	public : MenuItemLPFFreq(char const* newName = 0, int newP = 0) : MenuItemPatchedParamIntegerNonFM(newName, newP){}
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 50
		    && !soundEditor.currentParamManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(
		        PARAM_LOCAL_LPF_FREQ)) {
			numericDriver.setText("Off");
}
else MenuItemPatchedParamIntegerNonFM::drawValue(); }
#endif
}
lpfFreqMenu;

MenuItemPatchedParamIntegerNonFM lpfResMenu;
class MenuItemLPFMode final : public MenuItemSelection {
public:
	MenuItemLPFMode(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->lpfMode; }
	void writeCurrentValue() { soundEditor.currentModControllable->lpfMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"12dB", "24dB", "Drive", NULL};
		return options;
	}
	int getNumOptions() { return NUM_LPF_MODES; }
	bool isRelevant(Sound* sound, int whichThing) { return (!sound || sound->synthMode != SYNTH_MODE_FM); }
} lpfModeMenu;

// HPF menu ----------------------------------------------------------------------------------------------------

class MenuItemHPFFreq final : public MenuItemPatchedParamIntegerNonFM{
	public : MenuItemHPFFreq(char const* newName = 0, int newP = 0) : MenuItemPatchedParamIntegerNonFM(newName, newP){}
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 0
		    && !soundEditor.currentParamManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(
		        PARAM_LOCAL_HPF_FREQ)) {
			numericDriver.setText("OFF");
}
else MenuItemPatchedParamIntegerNonFM::drawValue(); }
#endif
}
hpfFreqMenu;

MenuItemPatchedParamIntegerNonFM hpfResMenu;

// Envelope menu ----------------------------------------------------------------------------------------------------
MenuItemSourceDependentPatchedParam envAttackMenu;
MenuItemSourceDependentPatchedParam envDecayMenu;
MenuItemSourceDependentPatchedParam envSustainMenu;
MenuItemSourceDependentPatchedParam envReleaseMenu;

// Voice menu ----------------------------------------------------------------------------------------------------
class MenuItemPolyphony final : public MenuItemSelection {
public:
	MenuItemPolyphony(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->polyphonic; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					soundDrum->polyphonic = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->polyphonic = soundEditor.currentValue;
		}
	}
	char const** getOptions() {
		static char const* options[] = {"Auto", "Polyphonic", "Monophonic", "Legato", "Choke", NULL};
		return options;
	}
	int getNumOptions() { // Hack-ish way of hiding the "choke" option when not editing a Kit
		if (soundEditor.editingKit()) return NUM_POLYPHONY_TYPES;
		else return NUM_POLYPHONY_TYPES - 1;
	}
	bool usesAffectEntire() { return true; }
} polyphonyMenu;

MenuItemSubmenu unisonMenu;
MenuItemUnpatchedParam portaMenu;
MenuItemArpeggiatorSubmenu arpMenu;

class MenuItemPriority final : public MenuItemSelection {
public:
	MenuItemPriority(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = *soundEditor.currentPriority; }
	void writeCurrentValue() { *soundEditor.currentPriority = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"LOW", "MEDIUM", "HIGH", NULL};
		return options;
	}
	int getNumOptions() { return NUM_PRIORITY_OPTIONS; }
} priorityMenu;

// Osc menu -------------------------------------------------------------------------------------------------------

class MenuItemRetriggerPhase final : public MenuItemDecimal {
public:
	MenuItemRetriggerPhase(char const* newName = NULL, bool newForModulator = false) : MenuItemDecimal(newName) {
		forModulator = newForModulator;
	}
	int getMinValue() { return -soundEditor.numberEditSize; }
	int getMaxValue() { return 360; }
	int getNumDecimalPlaces() { return 0; }
	int getDefaultEditPos() { return 1; }
	void readCurrentValue() {
		uint32_t value = *getValueAddress();
		if (value == 0xFFFFFFFF) soundEditor.currentValue = -soundEditor.numberEditSize;
		else soundEditor.currentValue = value / 11930464;
	}
	void writeCurrentValue() {
		uint32_t value;
		if (soundEditor.currentValue < 0) value = 0xFFFFFFFF;
		else value = soundEditor.currentValue * 11930464;
		*getValueAddress() = value;
	}
	void drawValue() {
		if (soundEditor.currentValue < 0) numericDriver.setText("OFF", false, 255, true);
		else MenuItemDecimal::drawValue();
	}
#if HAVE_OLED
	void drawPixelsForOled() {
		if (soundEditor.currentValue < 0) {
			OLED::drawStringCentred("OFF", 20, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_HUGE_SPACING_X,
			                        TEXT_HUGE_SIZE_Y);
		}
		else MenuItemDecimal::drawPixelsForOled();
	}
#endif
	void horizontalEncoderAction(int offset) {
		if (soundEditor.currentValue >= 0) MenuItemDecimal::horizontalEncoderAction(offset);
	}

	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		if (forModulator && sound->getSynthMode() != SYNTH_MODE_FM) return false;
		return (source->oscType != OSC_TYPE_SAMPLE || sound->getSynthMode() == SYNTH_MODE_FM);
	}

private:
	bool forModulator;
	uint32_t* getValueAddress() {
		if (forModulator) return &soundEditor.currentSound->modulatorRetriggerPhase[soundEditor.currentSourceIndex];
		else return &soundEditor.currentSound->oscRetriggerPhase[soundEditor.currentSourceIndex];
	}
};

MenuItemRetriggerPhase oscPhaseMenu;

class MenuItemSourceVolume final : public MenuItemSourceDependentPatchedParam {
public:
	MenuItemSourceVolume(char const* newName = 0, int newP = 0) : MenuItemSourceDependentPatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() != SYNTH_MODE_RINGMOD); }
} sourceVolumeMenu;

class MenuItemSourceWaveIndex final : public MenuItemSourceDependentPatchedParam {
public:
	MenuItemSourceWaveIndex(char const* newName = 0, int newP = 0)
	    : MenuItemSourceDependentPatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() != SYNTH_MODE_FM && source->oscType == OSC_TYPE_WAVETABLE);
	}
} sourceWaveIndexMenu;

class MenuItemSourceFeedback final : public MenuItemSourceDependentPatchedParam {
public:
	MenuItemSourceFeedback(char const* newName = 0, int newP = 0)
	    : MenuItemSourceDependentPatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() == SYNTH_MODE_FM); }
} sourceFeedbackMenu;

class MenuItemOscType final : public MenuItemSelection {
public:
	MenuItemOscType(char const* newName = NULL) : MenuItemSelection(newName) {
#if HAVE_OLED
		basicTitle = oscTypeTitle;
#endif
	}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		oscTypeTitle[3] = '1' + soundEditor.currentSourceIndex;
		MenuItemSelection::beginSession(navigatedBackwardFrom);
	}
#endif
	void readCurrentValue() {
		soundEditor.currentValue = soundEditor.currentSource->oscType;
	}
	void writeCurrentValue() {

		int oldValue = soundEditor.currentSource->oscType;
		int newValue = soundEditor.currentValue;

		if (oldValue == OSC_TYPE_INPUT_L || oldValue == OSC_TYPE_INPUT_R || oldValue == OSC_TYPE_INPUT_STEREO
		    || oldValue == OSC_TYPE_SAMPLE
		    || oldValue
		           == OSC_TYPE_WAVETABLE // Haven't actually really determined if this needs to be here - maybe not?

		    || newValue == OSC_TYPE_INPUT_L || newValue == OSC_TYPE_INPUT_R || newValue == OSC_TYPE_INPUT_STEREO
		    || newValue == OSC_TYPE_SAMPLE
		    || newValue
		           == OSC_TYPE_WAVETABLE) { // Haven't actually really determined if this needs to be here - maybe not?
			soundEditor.currentSound->unassignAllVoices();
		}

		soundEditor.currentSource->setOscType(newValue);
		if (oldValue == OSC_TYPE_SQUARE || newValue == OSC_TYPE_SQUARE)
			soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
	}

	//char const** getOptions() { static char const* options[] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "MMS1", "SUB1", "SAMPLE", "INL", "INR", "INLR", "SQ50", "SQ02", "SQ01", "SUB2", "SQ20", "SA50", "S101", "S303", "MMS2", "MMS3", "TABLE"}; return options; }
	char const** getOptions() {
#if HAVE_OLED
		static char inLText[] = "Input (left)";
		static char const* options[] = {"SINE",  "TRIANGLE",      "SQUARE",         "Analog square",
		                                "Saw",   "Analog saw",    "Wavetable",      "SAMPLE",
		                                inLText, "Input (right)", "Input (stereo)", NULL};
		inLText[5] =
		    ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn || DELUGE_MODEL == DELUGE_MODEL_40_PAD)) ? ' '
		                                                                                                         : 0;
#else
		static char inLText[4] = "INL";
		static char const* options[] = {"SINE",      "TRIANGLE", "SQUARE", "ASQUARE", "SAW", "ASAW",
		                                "Wavetable", "SAMPLE",   inLText,  "INR",     "INLR"};
		inLText[2] =
		    ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn || DELUGE_MODEL == DELUGE_MODEL_40_PAD)) ? 'L'
		                                                                                                         : 0;
#endif
		return options;
	}

	int getNumOptions() {
		if (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_RINGMOD) return NUM_OSC_TYPES_RINGMODDABLE;
		else if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn || DELUGE_MODEL == DELUGE_MODEL_40_PAD)
			return NUM_OSC_TYPES;
		else return NUM_OSC_TYPES - 2;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->getSynthMode() != SYNTH_MODE_FM);
	}
} oscTypeMenu;

class MenuItemAudioRecorder final : public MenuItem {
public:
	MenuItemAudioRecorder(char const* newName = 0) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		soundEditor.shouldGoUpOneLevelOnBegin = true;
		bool success = openUI(&audioRecorder);
		if (!success) {
			if (getCurrentUI() == &soundEditor) soundEditor.goUpOneLevel();
			uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
		}
		else audioRecorder.process();
	}
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (DELUGE_MODEL != DELUGE_MODEL_40_PAD && sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE);
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {

		bool can = isRelevant(sound, whichThing);
		if (!can) {
			numericDriver.displayPopup(HAVE_OLED ? "Can't record audio into an FM synth" : "CANT");
			return false;
		}

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, false, currentRange);
	}

} audioRecorderMenu;

class MenuItemSampleReverse final : public MenuItemSelectionSample {
public:
	MenuItemSampleReverse(char const* newName = NULL) : MenuItemSelectionSample(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->sampleControls.reversed; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					soundDrum->unassignAllVoices();
					source->setReversed(soundEditor.currentValue);
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->unassignAllVoices();
			soundEditor.currentSource->setReversed(soundEditor.currentValue);
		}
	}
} sampleReverseMenu;

class MenuItemSampleRepeat final : public MenuItemSelectionSample {
public:
	MenuItemSampleRepeat(char const* newName = NULL) : MenuItemSelectionSample(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->repeatMode; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
					if (soundEditor.currentValue == SAMPLE_REPEAT_STRETCH) {
						soundDrum->unassignAllVoices();
						source->sampleControls.pitchAndSpeedAreIndependent = true;
					}
					else if (source->repeatMode == SAMPLE_REPEAT_STRETCH) {
						soundDrum->unassignAllVoices();
						soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
					}

					source->repeatMode = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
			if (soundEditor.currentValue == SAMPLE_REPEAT_STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = true;
			}
			else if (soundEditor.currentSource->repeatMode == SAMPLE_REPEAT_STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
			}

			soundEditor.currentSource->repeatMode = soundEditor.currentValue;
		}

		// We need to re-render all rows, because this will have changed whether Note tails are displayed. Probably just one row, but we don't know which
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	}
	char const** getOptions() {
		static char const* options[] = {"CUT", "ONCE", "LOOP", "STRETCH", NULL};
		return options;
	}
	int getNumOptions() { return NUM_REPEAT_MODES; }
} sampleRepeatMenu;

class MenuItemSampleStart final : public MenuItemSampleLoopPoint {
public:
	MenuItemSampleStart(char const* newName = NULL) : MenuItemSampleLoopPoint(newName) { markerType = MARKER_START; }
} sampleStartMenu;

class MenuItemSampleEnd final : public MenuItemSampleLoopPoint {
public:
	MenuItemSampleEnd(char const* newName = NULL) : MenuItemSampleLoopPoint(newName) { markerType = MARKER_END; }
} sampleEndMenu;

class MenuItemSourceTranspose final : public MenuItemSourceDependentTranspose {
public:
	MenuItemSourceTranspose(char const* newName = NULL, int newP = 0)
	    : MenuItemSourceDependentTranspose(newName, newP) {}
	void readCurrentValue() {
		int transpose;
		int cents;
		if (soundEditor.currentMultiRange && soundEditor.currentSound->getSynthMode() != SYNTH_MODE_FM
		    && soundEditor.currentSource->oscType == OSC_TYPE_SAMPLE) {
			transpose = ((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.transpose;
			cents = ((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.cents;
		}
		else {
			transpose = soundEditor.currentSource->transpose;
			cents = soundEditor.currentSource->cents;
		}
		soundEditor.currentValue = transpose * 100 + cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;

		int transpose = semitones - 256;
		if (soundEditor.currentMultiRange && soundEditor.currentSound->getSynthMode() != SYNTH_MODE_FM
		    && soundEditor.currentSource->oscType == OSC_TYPE_SAMPLE) {
			((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.transpose = transpose;
			((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.setCents(cents);
		}
		else {
			soundEditor.currentSource->transpose = transpose;
			soundEditor.currentSource->setCents(cents);
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}
	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {

		if (!isRelevant(sound, whichThing)) {
			return MENU_PERMISSION_NO;
		}

		Source* source = &sound->sources[whichThing];

		if (sound->getSynthMode() == SYNTH_MODE_FM
		    || (source->oscType != OSC_TYPE_SAMPLE && source->oscType != OSC_TYPE_WAVETABLE))
			return MENU_PERMISSION_YES;

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, true, currentRange);
	}
	bool isRangeDependent() { return true; }
} sourceTransposeMenu;

class MenuItemSamplePitchSpeed final : public MenuItemSelectionSample {
public:
	MenuItemSamplePitchSpeed(char const* newName = NULL) : MenuItemSelectionSample(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() {
		soundEditor.currentValue = soundEditor.currentSampleControls->pitchAndSpeedAreIndependent;
	}
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					source->sampleControls.pitchAndSpeedAreIndependent = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSampleControls->pitchAndSpeedAreIndependent = soundEditor.currentValue;
		}
	}
	char const** getOptions() {
		static char const* options[] = {"Linked", "Independent", NULL};
		return options;
	}
} samplePitchSpeedMenu;

class MenuItemInterpolation final : public MenuItemSelection {
public:
	MenuItemInterpolation(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSampleControls->interpolationMode; }
	void writeCurrentValue() { soundEditor.currentSampleControls->interpolationMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Linear", "Sinc", NULL};
		return options;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		if (!sound) return true;
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE
		        && ((source->oscType == OSC_TYPE_SAMPLE && source->hasAtLeastOneAudioFileLoaded())
		            || source->oscType == OSC_TYPE_INPUT_L || source->oscType == OSC_TYPE_INPUT_R
		            || source->oscType == OSC_TYPE_INPUT_STEREO));
	}
} interpolationMenu;

class MenuItemTimeStretch final : public MenuItemInteger {
public:
	MenuItemTimeStretch(char const* newName = NULL) : MenuItemInteger(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->timeStretchAmount; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					source->timeStretchAmount = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSource->timeStretchAmount = soundEditor.currentValue;
		}
	}
	int getMinValue() { return -48; }
	int getMaxValue() { return 48; }
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE);
	}
} timeStretchMenu;

class MenuItemPulseWidth final : public MenuItemSourceDependentPatchedParam {
public:
	MenuItemPulseWidth(char const* newName = 0, int newP = 0) : MenuItemSourceDependentPatchedParam(newName, newP) {}
	int32_t getFinalValue() { return (uint32_t)soundEditor.currentValue * (85899345 >> 1); }
	void readCurrentValue() {
		soundEditor.currentValue =
		    ((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) * 100 + 2147483648) >> 32;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		if (sound->getSynthMode() == SYNTH_MODE_FM) return false;
		int oscType = sound->sources[whichThing].oscType;
		return (oscType != OSC_TYPE_SAMPLE && oscType != OSC_TYPE_INPUT_L && oscType != OSC_TYPE_INPUT_R
		        && oscType != OSC_TYPE_INPUT_STEREO);
	}
} pulseWidthMenu;

class MenuItemOscSync final : public MenuItemSelection {
public:
	MenuItemOscSync(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->oscillatorSync; }
	void writeCurrentValue() { soundEditor.currentSound->oscillatorSync = soundEditor.currentValue; }
	bool isRelevant(Sound* sound, int whichThing) {
		return (whichThing == 1 && sound->synthMode != SYNTH_MODE_FM && sound->sources[0].oscType != OSC_TYPE_SAMPLE
		        && sound->sources[1].oscType != OSC_TYPE_SAMPLE);
	}
} oscSyncMenu;

// Unison --------------------------------------------------------------------------------------
class MenuItemNumUnison final : public MenuItemInteger {
public:
	MenuItemNumUnison(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->numUnison; }
	void writeCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->setNumUnison(soundEditor.currentValue, modelStack);
	}
	int getMinValue() { return 1; }
	int getMaxValue() { return maxNumUnison; }
} numUnisonMenu;

class MenuItemUnisonDetune final : public MenuItemInteger {
public:
	MenuItemUnisonDetune(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->unisonDetune; }
	void writeCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->setUnisonDetune(soundEditor.currentValue, modelStack);
	}
	int getMaxValue() { return MAX_UNISON_DETUNE; }
} unisonDetuneMenu;

// Arp --------------------------------------------------------------------------------------
class MenuItemArpMode final : public MenuItemSelection {
public:
	MenuItemArpMode(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentArpSettings->mode; }
	void writeCurrentValue() {

		// If was off, or is now becoming off...
		if (soundEditor.currentArpSettings->mode == ARP_MODE_OFF || soundEditor.currentValue == ARP_MODE_OFF) {
			if (currentSong->currentClip->isActiveOnOutput()) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

				if (soundEditor.editingCVOrMIDIClip()) {
					((InstrumentClip*)currentSong->currentClip)
					    ->stopAllNotesForMIDIOrCV(modelStack->toWithTimelineCounter());
				}
				else {
					ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
					soundEditor.currentSound->allNotesOff(
					    modelStackWithSoundFlags,
					    soundEditor.currentSound->getArp()); // Must switch off all notes when switching arp on / off
					soundEditor.currentSound->reassessRenderSkippingStatus(modelStackWithSoundFlags);
				}
			}
		}
		soundEditor.currentArpSettings->mode = soundEditor.currentValue;

		// Only update the Clip-level arp setting if they hadn't been playing with other synth parameters first (so it's clear that switching the arp on or off was their main intention)
		if (!soundEditor.editingKit()) {
			bool arpNow = (soundEditor.currentValue != ARP_MODE_OFF); // Uh.... this does nothing...
		}
	}
	char const** getOptions() {
		static char const* options[] = {"OFF", "UP", "DOWN", "BOTH", "Random", NULL};
		return options;
	}
	int getNumOptions() { return NUM_ARP_MODES; }
} arpModeMenu;

class MenuItemArpSync final : public MenuItemSyncLevel {
public:
	MenuItemArpSync(char const* newName = NULL) : MenuItemSyncLevel(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentArpSettings->syncType,
		                                                        soundEditor.currentArpSettings->syncLevel);
	}
	void writeCurrentValue() {
		soundEditor.currentArpSettings->syncType = menuOptionToSyncType(soundEditor.currentValue);
		soundEditor.currentArpSettings->syncLevel = menuOptionToSyncLevel(soundEditor.currentValue);
	}
} arpSyncMenu;

class MenuItemArpOctaves final : public MenuItemInteger {
public:
	MenuItemArpOctaves(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentArpSettings->numOctaves; }
	void writeCurrentValue() { soundEditor.currentArpSettings->numOctaves = soundEditor.currentValue; }
	int getMinValue() { return 1; }
	int getMaxValue() { return 8; }
} arpOctavesMenu;

class MenuItemArpGate final : public MenuItemUnpatchedParam {
public:
	MenuItemArpGate(char const* newName = NULL, int newP = 0) : MenuItemUnpatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return !soundEditor.editingCVOrMIDIClip(); }
} arpGateMenu;

class MenuItemArpGateMIDIOrCV final : public MenuItemInteger {
public:
	MenuItemArpGateMIDIOrCV(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    (((int64_t)((InstrumentClip*)currentSong->currentClip)->arpeggiatorGate + 2147483648) * 50 + 2147483648)
		    >> 32;
	}
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->arpeggiatorGate =
		    (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
	}
	int getMaxValue() { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return soundEditor.editingCVOrMIDIClip(); }
} arpGateMenuMIDIOrCV;

class MenuItemArpRate final : public MenuItemPatchedParamInteger {
public:
	MenuItemArpRate(char const* newName = NULL, int newP = 0) : MenuItemPatchedParamInteger(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return !soundEditor.editingCVOrMIDIClip(); }
} arpRateMenu;

class MenuItemArpRateMIDIOrCV final : public MenuItemInteger {
public:
	MenuItemArpRateMIDIOrCV(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    (((int64_t)((InstrumentClip*)currentSong->currentClip)->arpeggiatorRate + 2147483648) * 50 + 2147483648)
		    >> 32;
	}
	void writeCurrentValue() {
		if (soundEditor.currentValue == 25) {
			((InstrumentClip*)currentSong->currentClip)->arpeggiatorRate = 0;
		}
		else {
			((InstrumentClip*)currentSong->currentClip)->arpeggiatorRate =
			    (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
		}
	}
	int getMaxValue() { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return soundEditor.editingCVOrMIDIClip(); }
} arpRateMenuMIDIOrCV;

// Modulator menu -----------------------------------------------------------------------
class MenuItemModulatorTranspose final : public MenuItemSourceDependentTranspose {
public:
	MenuItemModulatorTranspose(char const* newName = NULL, int newP = 0)
	    : MenuItemSourceDependentTranspose(newName, newP) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    (int32_t)soundEditor.currentSound->modulatorTranspose[soundEditor.currentSourceIndex] * 100
		    + soundEditor.currentSound->modulatorCents[soundEditor.currentSourceIndex];
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->setModulatorTranspose(soundEditor.currentSourceIndex, semitones - 256, modelStack);
		soundEditor.currentSound->setModulatorCents(soundEditor.currentSourceIndex, cents, modelStack);
	}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() == SYNTH_MODE_FM); }
} modulatorTransposeMenu;

class MenuItemSourceDependentPatchedParamFM final : public MenuItemSourceDependentPatchedParam {
public:
	MenuItemSourceDependentPatchedParamFM(char const* newName = 0, int newP = 0)
	    : MenuItemSourceDependentPatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() == SYNTH_MODE_FM); }
};

MenuItemSourceDependentPatchedParamFM modulatorVolume;

MenuItemSourceDependentPatchedParamFM modulatorFeedbackMenu;

class MenuItemModulatorDest final : public MenuItemSelection {
public:
	MenuItemModulatorDest(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->modulator1ToModulator0; }
	void writeCurrentValue() { soundEditor.currentSound->modulator1ToModulator0 = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Carriers", HAVE_OLED ? "Modulator 1" : "MOD1", NULL};
		return options;
	}
	bool isRelevant(Sound* sound, int whichThing) { return (whichThing == 1 && sound->synthMode == SYNTH_MODE_FM); }
} modulatorDestMenu;

MenuItemRetriggerPhase modulatorPhaseMenu;

// LFO1 menu ---------------------------------------------------------------------------------
class MenuItemLFO1Type final : public MenuItemLFOShape {
public:
	MenuItemLFO1Type(char const* newName = NULL) : MenuItemLFOShape(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->lfoGlobalWaveType; }
	void writeCurrentValue() { soundEditor.currentSound->setLFOGlobalWave(soundEditor.currentValue); }
} lfo1TypeMenu;

class MenuItemLFO1Rate final : public MenuItemPatchedParamInteger {
public:
	MenuItemLFO1Rate(char const* newName = 0, int newP = 0) : MenuItemPatchedParamInteger(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->lfoGlobalSyncLevel == 0); }
} lfo1RateMenu;

class MenuItemLFO1Sync final : public MenuItemSyncLevel {
public:
	MenuItemLFO1Sync(char const* newName = NULL) : MenuItemSyncLevel(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentSound->lfoGlobalSyncType,
		                                                        soundEditor.currentSound->lfoGlobalSyncLevel);
	}
	void writeCurrentValue() {
		soundEditor.currentSound->setLFOGlobalSyncType(menuOptionToSyncType(soundEditor.currentValue));
		soundEditor.currentSound->setLFOGlobalSyncLevel(menuOptionToSyncLevel(soundEditor.currentValue));
		soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
	}
} lfo1SyncMenu;

// LFO2 menu ---------------------------------------------------------------------------------
class MenuItemLFO2Type final : public MenuItemLFOShape {
public:
	MenuItemLFO2Type(char const* newName = NULL) : MenuItemLFOShape(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->lfoLocalWaveType; }
	void writeCurrentValue() { soundEditor.currentSound->lfoLocalWaveType = soundEditor.currentValue; }
} lfo2TypeMenu;

MenuItemPatchedParamInteger lfo2RateMenu;

// FX ----------------------------------------------------------------------------------------
MenuItemSubmenu modFXMenu;
MenuItemSubmenu eqMenu;
MenuItemSubmenu delayMenu;
MenuItemSubmenu reverbMenu;

class MenuItemClipping final : public MenuItemIntegerWithOff {
public:
	MenuItemClipping(char const* newName = NULL) : MenuItemIntegerWithOff(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->clippingAmount; }
	void writeCurrentValue() { soundEditor.currentModControllable->clippingAmount = soundEditor.currentValue; }
	int getMaxValue() { return 15; }
} clippingMenu;

MenuItemUnpatchedParam srrMenu;
MenuItemUnpatchedParam bitcrushMenu;

// Mod FX ----------------------------------------------------------------------------------

class MenuItemModFXType : public MenuItemSelection {
public:
	MenuItemModFXType(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->modFXType; }
	void writeCurrentValue() {
		if (!soundEditor.currentModControllable->setModFXType(soundEditor.currentValue)) {
			numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		}
	}
	char const** getOptions() {
		static char const* options[] = {"OFF", "FLANGER", "CHORUS", "PHASER", NULL};
		return options;
	}
	int getNumOptions() { return NUM_MOD_FX_TYPES; }
} modFXTypeMenu;

MenuItemPatchedParamInteger modFXRateMenu;

class MenuItemModFXFeedback final : public MenuItemUnpatchedParam {
public:
	MenuItemModFXFeedback(char const* newName = 0, int newP = 0) : MenuItemUnpatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) {
		return (!sound || sound->modFXType == MOD_FX_TYPE_FLANGER
		        || sound->modFXType == MOD_FX_TYPE_PHASER); // TODO: really want to receive a ModControllableAudio here!
	}
} modFXFeedbackMenu;

class MenuItemModFXDepth final : public MenuItemPatchedParamInteger {
public:
	MenuItemModFXDepth(char const* newName = 0, int newP = 0) : MenuItemPatchedParamInteger(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->modFXType == MOD_FX_TYPE_CHORUS || sound->modFXType == MOD_FX_TYPE_PHASER);
	}
} modFXDepthMenu;

class MenuItemModFXOffset final : public MenuItemUnpatchedParam {
public:
	MenuItemModFXOffset(char const* newName = 0, int newP = 0) : MenuItemUnpatchedParam(newName, newP) {}
	bool isRelevant(Sound* sound, int whichThing) {
		return (!sound
		        || sound->modFXType == MOD_FX_TYPE_CHORUS); // TODO: really want to receive a ModControllableAudio here!
	}
} modFXOffsetMenu;

// EQ -------------------------------------------------------------------------------------
MenuItemUnpatchedParam bassMenu;
MenuItemUnpatchedParam trebleMenu;
MenuItemUnpatchedParam bassFreqMenu;
MenuItemUnpatchedParam trebleFreqMenu;

// Delay ---------------------------------------------------------------------------------
MenuItemPatchedParamInteger delayFeedbackMenu;
MenuItemPatchedParamInteger delayRateMenu;

class MenuItemDelayPingPong final : public MenuItemSelection {
public:
	MenuItemDelayPingPong(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->delay.pingPong; }
	void writeCurrentValue() { soundEditor.currentModControllable->delay.pingPong = soundEditor.currentValue; }
} delayPingPongMenu;

class MenuItemDelayAnalog final : public MenuItemSelection {
public:
	MenuItemDelayAnalog(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->delay.analog; }
	void writeCurrentValue() { soundEditor.currentModControllable->delay.analog = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {
			"Digital",
#if HAVE_OLED
			"Analog",
			NULL
#else
			"ANA"
#endif
		};
		return options;
	}
} delayAnalogMenu;

class MenuItemDelaySync final : public MenuItemSyncLevel {
public:
	MenuItemDelaySync(char const* newName = NULL) : MenuItemSyncLevel(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->delay.sync; }
	void writeCurrentValue() { soundEditor.currentModControllable->delay.sync = soundEditor.currentValue; }
} delaySyncMenu;

// Reverb ----------------------------------------------------------------------------------
MenuItemPatchedParamInteger reverbAmountMenu;

class MenuItemReverbRoomSize final : public MenuItemInteger {
public:
	MenuItemReverbRoomSize(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = round(AudioEngine::reverb.getroomsize() * 50); }
	void writeCurrentValue() { AudioEngine::reverb.setroomsize((float)soundEditor.currentValue / 50); }
	int getMaxValue() { return 50; }
} reverbRoomSizeMenu;

class MenuItemReverbDampening final : public MenuItemInteger {
public:
	MenuItemReverbDampening(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = round(AudioEngine::reverb.getdamp() * 50); }
	void writeCurrentValue() { AudioEngine::reverb.setdamp((float)soundEditor.currentValue / 50); }
	int getMaxValue() { return 50; }
} reverbDampeningMenu;

class MenuItemReverbWidth final : public MenuItemInteger {
public:
	MenuItemReverbWidth(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = round(AudioEngine::reverb.getwidth() * 50); }
	void writeCurrentValue() { AudioEngine::reverb.setwidth((float)soundEditor.currentValue / 50); }
	int getMaxValue() { return 50; }
} reverbWidthMenu;

class MenuItemReverbPan final : public MenuItemInteger {
public:
	MenuItemReverbPan(char const* newName = NULL) : MenuItemInteger(newName) {}
	void drawValue() {
		char buffer[5];
		intToString(std::abs(soundEditor.currentValue), buffer, 1);
		if (soundEditor.currentValue < 0) strcat(buffer, "L");
		else if (soundEditor.currentValue > 0) strcat(buffer, "R");
		numericDriver.setText(buffer, true);
	}

	void writeCurrentValue() { AudioEngine::reverbPan = ((int32_t)soundEditor.currentValue * 33554432); }

	void readCurrentValue() { soundEditor.currentValue = ((int64_t)AudioEngine::reverbPan * 128 + 2147483648) >> 32; }
	int getMaxValue() { return 32; }
	int getMinValue() { return -32; }
} reverbPanMenu;

MenuItemCompressorSubmenu reverbCompressorMenu;

// Compressor --------------------------------------------------------------------------------
class MenuItemSidechainSend final : public MenuItemInteger {
public:
	MenuItemSidechainSend(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = ((uint64_t)soundEditor.currentSound->sideChainSendLevel * 50 + 1073741824) >> 31;
	}
	void writeCurrentValue() {
		if (soundEditor.currentValue == 50) soundEditor.currentSound->sideChainSendLevel = 2147483647;
		else soundEditor.currentSound->sideChainSendLevel = soundEditor.currentValue * 42949673;
	}
	int getMaxValue() { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return (soundEditor.editingKit()); }
} sidechainSendMenu;

class MenuItemReverbCompressorVolume final : public MenuItemInteger {
public:
	MenuItemReverbCompressorVolume(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() { soundEditor.currentValue = AudioEngine::reverbCompressorVolume / 21474836; }
	void writeCurrentValue() {
		AudioEngine::reverbCompressorVolume = soundEditor.currentValue * 21474836;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() { return 50; }
	int getMinValue() { return -1; }
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue < 0) numericDriver.setText("AUTO");
		else MenuItemInteger::drawValue();
	}
#endif
} reverbCompressorVolumeMenu;

class MenuItemCompressorVolumeShortcut final : public MenuItemFixedPatchCableStrength {
public:
	MenuItemCompressorVolumeShortcut(char const* newName = NULL, int newP = 0, int newS = 0)
	    : MenuItemFixedPatchCableStrength(newName, newP, newS) {}
	void writeCurrentValue() {
		MenuItemFixedPatchCableStrength::writeCurrentValue();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
} compressorVolumeShortcutMenu;

class MenuItemSidechainSync final : public MenuItemSyncLevel {
public:
	MenuItemSidechainSync(char const* newName = NULL) : MenuItemSyncLevel(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentCompressor->sync; }
	void writeCurrentValue() {
		soundEditor.currentCompressor->sync = soundEditor.currentValue;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		return !(soundEditor.editingReverbCompressor() && AudioEngine::reverbCompressorVolume < 0);
	}
} sidechainSyncMenu;

class MenuItemCompressorAttack final : public MenuItemInteger {
public:
	MenuItemCompressorAttack(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    getLookupIndexFromValue(soundEditor.currentCompressor->attack >> 2, attackRateTable, 50);
	}
	void writeCurrentValue() {
		soundEditor.currentCompressor->attack = attackRateTable[soundEditor.currentValue] << 2;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() { return 50; }
	bool isRelevant(Sound* sound, int whichThing) {
		return !(soundEditor.editingReverbCompressor() && AudioEngine::reverbCompressorVolume < 0);
	}
} compressorAttackMenu;

class MenuItemCompressorRelease final : public MenuItemInteger {
public:
	MenuItemCompressorRelease(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    getLookupIndexFromValue(soundEditor.currentCompressor->release >> 3, releaseRateTable, 50);
	}
	void writeCurrentValue() {
		soundEditor.currentCompressor->release = releaseRateTable[soundEditor.currentValue] << 3;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() { return 50; }
	bool isRelevant(Sound* sound, int whichThing) {
		return !(soundEditor.editingReverbCompressor() && AudioEngine::reverbCompressorVolume < 0);
	}
} compressorReleaseMenu;

MenuItemUnpatchedParamUpdatingReverbParams compressorShapeMenu;

class MenuItemReverbCompressorShape final : public MenuItemInteger {
public:
	MenuItemReverbCompressorShape(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = (((int64_t)AudioEngine::reverbCompressorShape + 2147483648) * 50 + 2147483648) >> 32;
	}
	void writeCurrentValue() {
		AudioEngine::reverbCompressorShape = (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return (AudioEngine::reverbCompressorVolume >= 0); }
} reverbCompressorShapeMenu;

class MenuItemBendRange : public MenuItemInteger {
public:
	MenuItemBendRange(char const* newName = NULL) : MenuItemInteger(newName) {}
	int getMaxValue() { return 96; }
};

class MenuItemBendRangeMain final : public MenuItemBendRange {
public:
	MenuItemBendRangeMain(char const* newName = NULL) : MenuItemBendRange(newName) {}
	void readCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		soundEditor.currentValue = expressionParams ? expressionParams->bendRanges[BEND_RANGE_MAIN]
		                                            : FlashStorage::defaultBendRange[BEND_RANGE_MAIN];
	}
	void writeCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		if (expressionParams) expressionParams->bendRanges[BEND_RANGE_MAIN] = soundEditor.currentValue;
	}
} mainBendRangeMenu;

class MenuItemBendRangePerFinger final : public MenuItemBendRange {
public:
	MenuItemBendRangePerFinger(char const* newName = NULL) : MenuItemBendRange(newName) {}
	void readCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		soundEditor.currentValue = expressionParams ? expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL]
		                                            : FlashStorage::defaultBendRange[BEND_RANGE_FINGER_LEVEL];
	}
	void writeCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		if (expressionParams) expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = soundEditor.currentValue;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		return soundEditor.navigationDepth == 1 || soundEditor.editingKit();
	}
} perFingerBendRangeMenu, drumBendRangeMenu;

// MIDI stuff
class MenuItemMIDIPreset : public MenuItemInteger {
public:
	MenuItemMIDIPreset(char const* newName = NULL) : MenuItemInteger(newName) {}
	int getMaxValue() { return 128; } // Probably not needed cos we override below...
#if HAVE_OLED
	void drawInteger(int textWidth, int textHeight, int yPixel) {
		char buffer[12];
		char const* text;
		if (soundEditor.currentValue == 128) text = "NONE";
		else {
			intToString(soundEditor.currentValue + 1, buffer, 1);
			text = buffer;
		}
		OLED::drawStringCentred(text, yPixel + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                        textWidth, textHeight);
	}
#else
	void drawValue() {
		if (soundEditor.currentValue == 128) numericDriver.setText("NONE");
		else numericDriver.setTextAsNumber(soundEditor.currentValue + 1);
	}
#endif
	bool isRelevant(Sound* sound, int whichThing) {
		return currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT;
	}
	void selectEncoderAction(int offset) {
		soundEditor.currentValue += offset;
		if (soundEditor.currentValue >= 129) soundEditor.currentValue -= 129;
		else if (soundEditor.currentValue < 0) soundEditor.currentValue += 129;
		MenuItemNumber::selectEncoderAction(offset);
	}
};

class MenuItemMIDIBank final : public MenuItemMIDIPreset {
public:
	MenuItemMIDIBank(char const* newName = NULL) : MenuItemMIDIPreset(newName) {}
	void readCurrentValue() { soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->midiBank; }
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->midiBank = soundEditor.currentValue;
		if (((InstrumentClip*)currentSong->currentClip)->isActiveOnOutput()) {
			((InstrumentClip*)currentSong->currentClip)->sendMIDIPGM();
		}
	}
} midiBankMenu;

class MenuItemMIDISub final : public MenuItemMIDIPreset {
public:
	MenuItemMIDISub(char const* newName = NULL) : MenuItemMIDIPreset(newName) {}
	void readCurrentValue() { soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->midiSub; }
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->midiSub = soundEditor.currentValue;
		if (((InstrumentClip*)currentSong->currentClip)->isActiveOnOutput()) {
			((InstrumentClip*)currentSong->currentClip)->sendMIDIPGM();
		}
	}
} midiSubMenu;

class MenuItemMIDIPGM final : public MenuItemMIDIPreset {
public:
	MenuItemMIDIPGM(char const* newName = NULL) : MenuItemMIDIPreset(newName) {}
	void readCurrentValue() { soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->midiPGM; }
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->midiPGM = soundEditor.currentValue;
		if (((InstrumentClip*)currentSong->currentClip)->isActiveOnOutput()) {
			((InstrumentClip*)currentSong->currentClip)->sendMIDIPGM();
		}
	}
} midiPGMMenu;

char const* sequenceDirectionOptions[] = {"FORWARD", "REVERSED", "PING-PONG", NULL, NULL};

// Clip-level stuff
class MenuItemSequenceDirection final : public MenuItemSelection {
public:
	MenuItemSequenceDirection(char const* newName = NULL) : MenuItemSelection(newName) {}

	ModelStackWithNoteRow* getIndividualNoteRow(ModelStackWithTimelineCounter* modelStack) {
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
		if (!clip->affectEntire && clip->output->type == INSTRUMENT_TYPE_KIT) {
			Kit* kit = (Kit*)currentSong->currentClip->output;
			if (kit->selectedDrum) {
				return clip->getNoteRowForDrum(modelStack, kit->selectedDrum); // Still might be NULL;
			}
		}
		return modelStack->addNoteRow(0, NULL);
	}

	void readCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			soundEditor.currentValue = modelStackWithNoteRow->getNoteRow()->sequenceDirectionMode;
		}
		else {
			soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->sequenceDirectionMode;
		}
	}

	void writeCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			modelStackWithNoteRow->getNoteRow()->setSequenceDirectionMode(modelStackWithNoteRow,
			                                                              soundEditor.currentValue);
		}
		else {
			((InstrumentClip*)currentSong->currentClip)
			    ->setSequenceDirectionMode(modelStackWithNoteRow->toWithTimelineCounter(), soundEditor.currentValue);
		}
	}
	int getNumOptions() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		return modelStackWithNoteRow->getNoteRowAllowNull() ? 4 : 3;
	}
	char const** getOptions() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* modelStackWithNoteRow = getIndividualNoteRow(modelStack);
		sequenceDirectionOptions[3] = modelStackWithNoteRow->getNoteRowAllowNull() ? "NONE" : NULL;
		return sequenceDirectionOptions;
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {
		if (!((InstrumentClip*)currentSong->currentClip)->affectEntire
		    && currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT
		    && !((Kit*)currentSong->currentClip->output)->selectedDrum) {
			return MENU_PERMISSION_NO;
		}
		else {
			return MENU_PERMISSION_YES;
		}
	}

} sequenceDirectionMenu;

// AudioClip stuff
class MenuItemAudioClipReverse final : public MenuItemSelection {
public:
	MenuItemAudioClipReverse(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = ((AudioClip*)currentSong->currentClip)->sampleControls.reversed;
	}
	void writeCurrentValue() {
		AudioClip* clip = (AudioClip*)currentSong->currentClip;
		bool active = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip) && clip->voiceSample);

		clip->unassignVoiceSample();

		clip->sampleControls.reversed = soundEditor.currentValue;

		if (clip->sampleHolder.audioFile) {
			if (clip->sampleControls.reversed) {
				uint64_t lengthInSamples = ((Sample*)clip->sampleHolder.audioFile)->lengthInSamples;
				if (clip->sampleHolder.endPos > lengthInSamples) {
					clip->sampleHolder.endPos = lengthInSamples;
				}
			}

			clip->sampleHolder.claimClusterReasons(clip->sampleControls.reversed);

			if (active) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				clip->resumePlayback(modelStack, true);
			}

			uiNeedsRendering(&audioClipView, 0xFFFFFFFF, 0);
		}
	}
} audioClipReverseMenu;

class MenuItemAudioClipTranspose final : public MenuItemDecimal, public MenuItemWithCCLearning {
public:
	MenuItemAudioClipTranspose(char const* newName = NULL) : MenuItemDecimal(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = ((AudioClip*)currentSong->currentClip)->sampleHolder.transpose * 100
		                           + ((AudioClip*)currentSong->currentClip)->sampleHolder.cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;
		int transpose = semitones - 256;

		((AudioClip*)currentSong->currentClip)->sampleHolder.transpose = transpose;
		((AudioClip*)currentSong->currentClip)->sampleHolder.cents = cents;

		((AudioClip*)currentSong->currentClip)->sampleHolder.recalculateNeutralPhaseIncrement();
	}

	int getMinValue() { return -9600; }
	int getMaxValue() { return 9600; }
	int getNumDecimalPlaces() { return 2; }

	void unlearnAction() { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	ParamDescriptor getLearningThing() {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST);
		return paramDescriptor;
	}

} audioClipTransposeMenu;

class MenuItemAudioClipAttack final : public MenuItemInteger {
public:
	MenuItemAudioClipAttack(char const* newName = NULL) : MenuItemInteger(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    (((int64_t)((AudioClip*)currentSong->currentClip)->attack + 2147483648) * 50 + 2147483648) >> 32;
	}
	void writeCurrentValue() {
		((AudioClip*)currentSong->currentClip)->attack = (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
	}
	int getMaxValue() { return 50; }
} audioClipAttackMenu;

MenuItemSubmenu audioClipSampleMenu;

MenuItemAudioClipSampleMarkerEditor audioClipSampleMarkerEditorMenuStart;
MenuItemAudioClipSampleMarkerEditor audioClipSampleMarkerEditorMenuEnd;

MenuItemSubmenu audioClipLPFMenu;
MenuItemUnpatchedParam audioClipLPFResMenu;

class MenuItemAudioClipLPFFreq final : public MenuItemUnpatchedParam{
	public : MenuItemAudioClipLPFFreq(char const* newName = 0, int newP = 0) : MenuItemUnpatchedParam(newName, newP){}
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 50) {
			numericDriver.setText("OFF");
}
else MenuItemUnpatchedParam::drawValue(); }
#endif
}
audioClipLPFFreqMenu;

MenuItemSubmenu audioClipHPFMenu;
MenuItemUnpatchedParam audioClipHPFResMenu;

class MenuItemAudioClipHPFFreq final : public MenuItemUnpatchedParam{
	public : MenuItemAudioClipHPFFreq(char const* newName = 0, int newP = 0) : MenuItemUnpatchedParam(newName, newP){}
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 0) {
			numericDriver.setText("OFF");
}
else MenuItemUnpatchedParam::drawValue(); }
#endif
}
audioClipHPFFreqMenu;

MenuItemSubmenu audioClipCompressorMenu;
MenuItemUnpatchedParamUpdatingReverbParams audioClipCompressorVolumeMenu;

MenuItemSubmenu audioClipFXMenu;

MenuItemSubmenu audioClipModFXMenu;
MenuItemUnpatchedParam audioClipModFXDepthMenu;
MenuItemUnpatchedParam audioClipModFXRateMenu;

class MenuItemAudioClipModFXType final : public MenuItemModFXType {
public:
	MenuItemAudioClipModFXType(char const* newName = NULL) : MenuItemModFXType(newName) {}
	void selectEncoderAction(
	    int offset) { // We override this to set min value to 1. We don't inherit any getMinValue() function to override more easily
		soundEditor.currentValue += offset;
		int numOptions = getNumOptions();

		if (soundEditor.currentValue >= numOptions) soundEditor.currentValue -= (numOptions - 1);
		else if (soundEditor.currentValue < 1) soundEditor.currentValue += (numOptions - 1);

		MenuItemValue::selectEncoderAction(offset);
	}
} audioClipModFXTypeMenu;

MenuItemSubmenu audioClipReverbMenu;
MenuItemUnpatchedParam audioClipReverbSendAmountMenu;

MenuItemSubmenu audioClipDelayMenu;
MenuItemUnpatchedParam audioClipDelayFeedbackMenu;
MenuItemUnpatchedParam audioClipDelayRateMenu;

MenuItemUnpatchedParam audioClipLevelMenu;
MenuItemUnpatchedParamPan audioClipPanMenu;

MenuItemFixedPatchCableStrength vibratoMenu;

#define comingSoonMenu (MenuItem*)0xFFFFFFFF

const MenuItem* midiOrCVParamShortcuts[8] = {
    &arpRateMenuMIDIOrCV, &arpSyncMenu, &arpGateMenuMIDIOrCV, &arpOctavesMenu, &arpModeMenu, NULL, NULL, NULL,
};

MenuItem* paramShortcutsForSounds[][8] = {
    // Pre V3
    {&sampleRepeatMenu, &sampleReverseMenu, &timeStretchMenu, &samplePitchSpeedMenu, &audioRecorderMenu,
     &fileSelectorMenu, &sampleEndMenu, &sampleStartMenu},
    {&sampleRepeatMenu, &sampleReverseMenu, &timeStretchMenu, &samplePitchSpeedMenu, &audioRecorderMenu,
     &fileSelectorMenu, &sampleEndMenu, &sampleStartMenu},
    {&sourceVolumeMenu, &sourceTransposeMenu, &oscTypeMenu, &pulseWidthMenu, &oscPhaseMenu, &sourceFeedbackMenu,
     &noiseMenu, &sourceWaveIndexMenu},
    {&sourceVolumeMenu, &sourceTransposeMenu, &oscTypeMenu, &pulseWidthMenu, &oscPhaseMenu, &sourceFeedbackMenu,
     &oscSyncMenu, &sourceWaveIndexMenu},
    {&modulatorVolume, &modulatorTransposeMenu, comingSoonMenu, comingSoonMenu, &modulatorPhaseMenu,
     &modulatorFeedbackMenu, comingSoonMenu, &sequenceDirectionMenu},
    {&modulatorVolume, &modulatorTransposeMenu, comingSoonMenu, comingSoonMenu, &modulatorPhaseMenu,
     &modulatorFeedbackMenu, &modulatorDestMenu, NULL},
    {&volumeMenu, &masterTransposeMenu, &vibratoMenu, &panMenu, &synthModeMenu, &srrMenu, &bitcrushMenu, &clippingMenu},
    {&portaMenu, &polyphonyMenu, &priorityMenu, &unisonDetuneMenu, &numUnisonMenu, NULL, NULL, NULL},
    {&envReleaseMenu, &envSustainMenu, &envDecayMenu, &envAttackMenu, NULL, &lpfModeMenu, &lpfResMenu, &lpfFreqMenu},
    {&envReleaseMenu, &envSustainMenu, &envDecayMenu, &envAttackMenu, NULL, comingSoonMenu, &hpfResMenu, &hpfFreqMenu},
    {&compressorReleaseMenu, &sidechainSyncMenu, &compressorVolumeShortcutMenu, &compressorAttackMenu,
     &compressorShapeMenu, &sidechainSendMenu, &bassMenu, &bassFreqMenu},
    {&arpRateMenu, &arpSyncMenu, &arpGateMenu, &arpOctavesMenu, &arpModeMenu, &drumNameMenu, &trebleMenu,
     &trebleFreqMenu},
    {&lfo1RateMenu, &lfo1SyncMenu, &lfo1TypeMenu, &modFXTypeMenu, &modFXOffsetMenu, &modFXFeedbackMenu, &modFXDepthMenu,
     &modFXRateMenu},
    {&lfo2RateMenu, comingSoonMenu, &lfo2TypeMenu, &reverbAmountMenu, &reverbPanMenu, &reverbWidthMenu,
     &reverbDampeningMenu, &reverbRoomSizeMenu},
    {&delayRateMenu, &delaySyncMenu, &delayAnalogMenu, &delayFeedbackMenu, &delayPingPongMenu, NULL, NULL, NULL}};

MenuItem* paramShortcutsForAudioClips[][8] = {
    {NULL, &audioClipReverseMenu, NULL, &samplePitchSpeedMenu, NULL, &fileSelectorMenu,
     &audioClipSampleMarkerEditorMenuEnd, &audioClipSampleMarkerEditorMenuStart},
    {NULL, &audioClipReverseMenu, NULL, &samplePitchSpeedMenu, NULL, &fileSelectorMenu,
     &audioClipSampleMarkerEditorMenuEnd, &audioClipSampleMarkerEditorMenuStart},
    {&audioClipLevelMenu, &audioClipTransposeMenu, NULL, NULL, NULL, NULL, NULL, NULL},
    {&audioClipLevelMenu, &audioClipTransposeMenu, NULL, NULL, NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {&audioClipLevelMenu, &audioClipTransposeMenu, NULL, &audioClipPanMenu, NULL, &srrMenu, &bitcrushMenu,
     &clippingMenu},
    {NULL, NULL, &priorityMenu, NULL, NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, &audioClipAttackMenu, NULL, &lpfModeMenu, &audioClipLPFResMenu, &audioClipLPFFreqMenu},
    {NULL, NULL, NULL, &audioClipAttackMenu, NULL, comingSoonMenu, &audioClipHPFResMenu, &audioClipHPFFreqMenu},
    {&compressorReleaseMenu, &sidechainSyncMenu, &audioClipCompressorVolumeMenu, &compressorAttackMenu,
     &compressorShapeMenu, NULL, &bassMenu, &bassFreqMenu},
    {NULL, NULL, NULL, NULL, NULL, NULL, &trebleMenu, &trebleFreqMenu},
    {NULL, NULL, NULL, &audioClipModFXTypeMenu, &modFXOffsetMenu, &modFXFeedbackMenu, &audioClipModFXDepthMenu,
     &audioClipModFXRateMenu},
    {NULL, NULL, NULL, &audioClipReverbSendAmountMenu, &reverbPanMenu, &reverbWidthMenu, &reverbDampeningMenu,
     &reverbRoomSizeMenu},
    {&audioClipDelayRateMenu, &delaySyncMenu, &delayAnalogMenu, &audioClipDelayFeedbackMenu, &delayPingPongMenu, NULL,
     NULL, NULL}};

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

		paramShortcutsForAudioClips[0][7] = &audioClipSampleMarkerEditorMenuEnd;
		paramShortcutsForAudioClips[1][7] = &audioClipSampleMarkerEditorMenuEnd;

		paramShortcutsForAudioClips[0][6] = &interpolationMenu;
		paramShortcutsForAudioClips[1][6] = &interpolationMenu;

		paramShortcutsForSounds[0][6] = &interpolationMenu;
		paramShortcutsForSounds[1][6] = &interpolationMenu;

		paramShortcutsForSounds[2][6] = &sourceWaveIndexMenu;
		paramShortcutsForSounds[3][6] = &sourceWaveIndexMenu;

		paramShortcutsForSounds[2][7] = &noiseMenu;
		paramShortcutsForSounds[3][7] = &oscSyncMenu;
		modSourceShortcuts[0][7] = PATCH_SOURCE_X;
		modSourceShortcuts[1][7] = PATCH_SOURCE_Y;
	}
}

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

char const* firmwareString = "4.1.4-alpha3";

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

SoundEditor::SoundEditor() {
	currentParamShorcutX = 255;
	memset(sourceShortcutBlinkFrequencies, 255, sizeof(sourceShortcutBlinkFrequencies));
	timeLastAttemptedAutomatedParamEdit = 0;
	shouldGoUpOneLevelOnBegin = false;

	// Settings menu -------------------------------------------------------------
	// Trigger clock in menu
	new (&triggerInPPQNMenu) MenuItemTriggerInPPQN("PPQN");
	new (&triggerInAutoStartMenu) MenuItemTriggerInAutoStart("Auto-start");
	static MenuItem* triggerClockInMenuItems[] = {&triggerInPPQNMenu, &triggerInAutoStartMenu, NULL};

	// Trigger clock out menu
	new (&triggerOutPPQNMenu) MenuItemTriggerOutPPQN("PPQN");
	static MenuItem* triggerClockOutMenuItems[] = {&triggerOutPPQNMenu, NULL};

	// MIDI clock menu
	new (&midiClockInStatusMenu) MenuItemMidiClockInStatus(HAVE_OLED ? "Input" : "IN");
	new (&midiClockOutStatusMenu) MenuItemMidiClockOutStatus(HAVE_OLED ? "Output" : "OUT");
	new (&tempoMagnitudeMatchingMenu) MenuItemTempoMagnitudeMatching(HAVE_OLED ? "Tempo magnitude matching" : "MAGN");
	static MenuItem* midiClockMenuItems[] = {&midiClockInStatusMenu, &midiClockOutStatusMenu,
	                                         &tempoMagnitudeMatchingMenu, NULL};

	// MIDI commands menu
	new (&playbackRestartMidiCommand) MenuItemMidiCommand("Restart", GLOBAL_MIDI_COMMAND_PLAYBACK_RESTART);
	new (&playMidiCommand) MenuItemMidiCommand("PLAY", GLOBAL_MIDI_COMMAND_PLAY);
	new (&recordMidiCommand) MenuItemMidiCommand(HAVE_OLED ? "Record" : "REC", GLOBAL_MIDI_COMMAND_RECORD);
	new (&tapMidiCommand) MenuItemMidiCommand("Tap tempo", GLOBAL_MIDI_COMMAND_TAP);
	new (&undoMidiCommand) MenuItemMidiCommand("UNDO", GLOBAL_MIDI_COMMAND_UNDO);
	new (&redoMidiCommand) MenuItemMidiCommand("REDO", GLOBAL_MIDI_COMMAND_REDO);
	new (&loopMidiCommand) MenuItemMidiCommand("LOOP", GLOBAL_MIDI_COMMAND_LOOP);
	new (&loopContinuousLayeringMidiCommand)
	    MenuItemMidiCommand("LAYERING loop", GLOBAL_MIDI_COMMAND_LOOP_CONTINUOUS_LAYERING);
	static MenuItem* midiCommandsMenuItems[] = {&playMidiCommand,
	                                            &playbackRestartMidiCommand,
	                                            &recordMidiCommand,
	                                            &tapMidiCommand,
	                                            &undoMidiCommand,
	                                            &redoMidiCommand,
	                                            &loopMidiCommand,
	                                            &loopContinuousLayeringMidiCommand,
	                                            NULL};

	// MIDI menu
	new (&midiClockMenu) MenuItemSubmenu("CLOCK", midiClockMenuItems);
	new (&midiThruMenu) MenuItemMidiThru(HAVE_OLED ? "MIDI-thru" : "THRU");
	new (&midiCommandsMenu) MenuItemSubmenu(HAVE_OLED ? "Commands" : "CMD", midiCommandsMenuItems);
	new (&midiInputDifferentiationMenu) MenuItemMidiInputDifferentiation("Differentiate inputs");
	new (&midiDevicesMenu) MenuItemMIDIDevices("Devices");
	static MenuItem* midiMenuItems[] = {
	    &midiClockMenu, &midiThruMenu, &midiCommandsMenu, &midiInputDifferentiationMenu, &midiDevicesMenu, NULL};

	new (&mpeZoneNumMemberChannelsMenu) MenuItemMPEZoneNumMemberChannels();
	new (&mpeZoneSelectorMenu) MenuItemMPEZoneSelector();
	new (&mpeDirectionSelectorMenu) MenuItemMPEDirectionSelector("MPE");
	new (&defaultVelocityToLevelMenu) MenuItemDefaultVelocityToLevel("VELOCITY");
	static MenuItem* midiDeviceMenuItems[] = {&mpeDirectionSelectorMenu, &defaultVelocityToLevelMenu, NULL};

	new (&midiDeviceMenu) MenuItemSubmenu(NULL, midiDeviceMenuItems);

	// Trigger clock menu
	new (&triggerClockInMenu) MenuItemSubmenu(HAVE_OLED ? "Input" : "IN", triggerClockInMenuItems);
	new (&triggerClockOutMenu) MenuItemSubmenu(HAVE_OLED ? "Output" : "OUT", triggerClockOutMenuItems);
	static MenuItem* triggerClockMenuItems[] = {&triggerClockInMenu, &triggerClockOutMenu, NULL};

	// Defaults menu
	new (&defaultTempoMenu) MenuItemIntegerRange("TEMPO", 60, 240);
	new (&defaultSwingMenu) MenuItemIntegerRange("SWING", 1, 99);
	new (&defaultKeyMenu) MenuItemKeyRange("KEY");
	new (&defaultScaleMenu) MenuItemDefaultScale("SCALE");
	new (&defaultVelocityMenu) MenuItemDefaultVelocity("VELOCITY");
	new (&defaultMagnitudeMenu) MenuItemDefaultMagnitude("RESOLUTION");
	new (&defaultBendRangeMenu) MenuItemBendRangeDefault("Bend range");
	static MenuItem* defaultsMenuItems[] = {
	    &defaultTempoMenu,    &defaultSwingMenu,     &defaultKeyMenu,       &defaultScaleMenu,
	    &defaultVelocityMenu, &defaultMagnitudeMenu, &defaultBendRangeMenu, NULL};

	// Record menu
	new (&recordQuantizeMenu) MenuItemRecordQuantize("Quantization");
	new (&recordMarginsMenu) MenuItemRecordMargins(HAVE_OLED ? "Loop margins" : "MARGINS");
	new (&recordCountInMenu) MenuItemRecordCountIn("Count-in");
	new (&monitorModeMenu) MenuItemMonitorMode(HAVE_OLED ? "Sampling monitoring" : "MONITORING");
	static MenuItem* recordMenuItems[] = {&recordCountInMenu, &recordQuantizeMenu, &recordMarginsMenu, &monitorModeMenu,
	                                      NULL};

	// Colours submenu
	new (&activeColourMenu) MenuItemColour("ACTIVE");
	new (&stoppedColourMenu) MenuItemColour("STOPPED");
	new (&mutedColourMenu) MenuItemColour("MUTED");
	new (&soloColourMenu) MenuItemColour("SOLOED");
	static MenuItem* coloursMenuItems[] = {&activeColourMenu, &stoppedColourMenu, &mutedColourMenu, &soloColourMenu,
	                                       NULL};

	// Pads menu
	new (&shortcutsVersionMenu) MenuItemShortcutsVersion(HAVE_OLED ? "Shortcuts version" : "SHOR");
	new (&keyboardLayoutMenu) MenuItemKeyboardLayout(HAVE_OLED ? "Keyboard for text" : "KEYB");
	new (&coloursSubmenu) MenuItemSubmenu("COLOURS", coloursMenuItems);
	static MenuItem* layoutMenuItems[] = {&shortcutsVersionMenu, &keyboardLayoutMenu, &coloursSubmenu, NULL};

	// Root menu
	new (&cvSelectionMenu) MenuItemCVSelection("CV");
	new (&gateSelectionMenu) MenuItemGateSelection("GATE");
	new (&triggerClockMenu) MenuItemSubmenu(HAVE_OLED ? "Trigger clock" : "TCLOCK", triggerClockMenuItems);
	new (&midiMenu) MenuItemSubmenu("MIDI", midiMenuItems);
	new (&defaultsSubmenu) MenuItemSubmenu("DEFAULTS", defaultsMenuItems);
	new (&swingIntervalMenu) MenuItemSwingInterval(HAVE_OLED ? "Swing interval" : "SWIN");
	new (&padsSubmenu) MenuItemSubmenu("PADS", layoutMenuItems);
	new (&sampleBrowserPreviewModeMenu) MenuItemSampleBrowserPreviewMode(HAVE_OLED ? "Sample preview" : "PREV");
	new (&flashStatusMenu) MenuItemFlashStatus(HAVE_OLED ? "Play-cursor" : "CURS");
	new (&recordSubmenu) MenuItemSubmenu("Recording", recordMenuItems);
	new (&firmwareVersionMenu) MenuItemFirmwareVersion("Firmware version");

	static MenuItem* rootSettingsMenuItems[] = {
	    &cvSelectionMenu, &gateSelectionMenu, &triggerClockMenu,    &midiMenu,
	    &defaultsSubmenu, &swingIntervalMenu, &padsSubmenu,         &sampleBrowserPreviewModeMenu,
	    &flashStatusMenu, &recordSubmenu,     &firmwareVersionMenu, NULL};
	new (&settingsRootMenu) MenuItemSubmenu("Settings", rootSettingsMenuItems);

	// CV menu
	new (&cvVoltsMenu) MenuItemCVVolts("Volts per octave");
	new (&cvTransposeMenu) MenuItemCVTranspose("TRANSPOSE");
	static MenuItem* cvMenuItems[] = {&cvVoltsMenu, &cvTransposeMenu, NULL};

	new (&cvSubmenu) MenuItemSubmenu("", cvMenuItems);

	// Gate stuff
	new (&gateOffTimeMenu) MenuItemGateOffTime(HAVE_OLED ? "Min. off-time" : "");
	new (&gateModeMenu) MenuItemGateMode();

#if HAVE_OLED
	triggerClockInMenu.basicTitle = "T. clock input";
	triggerClockOutMenu.basicTitle = "T. clock out";
	triggerInPPQNMenu.basicTitle = "Input PPQN";
	triggerOutPPQNMenu.basicTitle = "Output PPQN";

	midiClockMenu.basicTitle = "MIDI clock";
	midiClockInStatusMenu.basicTitle = "MIDI clock in";
	midiClockOutStatusMenu.basicTitle = "MIDI clock out";
	tempoMagnitudeMatchingMenu.basicTitle = "Tempo m. match";
	midiCommandsMenu.basicTitle = "MIDI commands";
	midiDevicesMenu.basicTitle = "MIDI devices";

	defaultTempoMenu.basicTitle = "Default tempo";
	defaultSwingMenu.basicTitle = "Default swing";
	defaultKeyMenu.basicTitle = "Default key";
	defaultScaleMenu.basicTitle = "Default scale";
	defaultVelocityMenu.basicTitle = "Default veloc.";
	defaultMagnitudeMenu.basicTitle = "Default resol.";
	defaultBendRangeMenu.basicTitle = "Default bend r";

	shortcutsVersionMenu.basicTitle = "Shortcuts ver.";
	keyboardLayoutMenu.basicTitle = "Key layout";

	recordCountInMenu.basicTitle = "Rec count-in";
	monitorModeMenu.basicTitle = "Monitoring";
	firmwareVersionMenu.basicTitle = "Firmware ver.";
#endif

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
	new (&audioClipReverseMenu) MenuItemAudioClipReverse("REVERSE");
	new (&audioClipSampleMarkerEditorMenuStart) MenuItemAudioClipSampleMarkerEditor("", MARKER_START);
	new (&audioClipSampleMarkerEditorMenuEnd) MenuItemAudioClipSampleMarkerEditor("WAVEFORM", MARKER_END);
	static MenuItem* audioClipSampleMenuItems[] = {&fileSelectorMenu,     &audioClipReverseMenu,
	                                               &samplePitchSpeedMenu, &audioClipSampleMarkerEditorMenuEnd,
	                                               &interpolationMenu,    NULL};

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
}

bool SoundEditor::editingKit() {
	return currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT;
}

bool SoundEditor::editingCVOrMIDIClip() {
	return (currentSong->currentClip->output->type == INSTRUMENT_TYPE_MIDI_OUT
	        || currentSong->currentClip->output->type == INSTRUMENT_TYPE_CV);
}

bool SoundEditor::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (getRootUI() == &keyboardScreen) return false;
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
	if (!success)
		return true; // Must return true, which means everything is dealt with - because this UI would already have been exited if there was a problem

	setLedStates();

	return true;
}

void SoundEditor::focusRegained() {

	// If just came back from a deeper nested UI...
	if (shouldGoUpOneLevelOnBegin) {
		goUpOneLevel();
		shouldGoUpOneLevelOnBegin = false;

		// If that already exited this UI, then get out now before setting any LEDs
		if (getCurrentUI() != this) return;

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
				if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				MenuItem* newItem = getCurrentMenuItem()->selectButtonPress();
				if (newItem) {
					if (newItem != (MenuItem*)0xFFFFFFFF) {

						int result = newItem->checkPermissionToBeginSession(currentSound, currentSourceIndex,
						                                                    &currentMultiRange);

						if (result != MENU_PERMISSION_NO) {

							if (result == MENU_PERMISSION_MUST_SELECT_RANGE) {
								currentMultiRange = NULL;
								multiRangeMenu.menuItemHeadingTo = newItem;
								newItem = &multiRangeMenu;
							}

							navigationDepth++;
							menuItemNavigationRecord[navigationDepth] = newItem;
							numericDriver.setNextTransitionDirection(1);
							beginScreen();
						}
					}
				}
				else goUpOneLevel();
			}
		}
	}

	// Back button
	else if (x == backButtonX && y == backButtonY) {
		if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING) {
			if (on) {
				if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

				// Special case if we're editing a range
				if (getCurrentMenuItem() == &multiRangeMenu && multiRangeMenu.cancelEditingIfItsOn()) {}
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
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (Buttons::isShiftButtonPressed()) {
				if (getCurrentMenuItem() == &multiRangeMenu) {
					multiRangeMenu.deletePress();
				}
			}
			else {
				openUI(&saveInstrumentPresetUI);
			}
		}
	}

	// MIDI learn button
	else if (x == learnButtonX && y == learnButtonY) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
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

			if (currentUIMode == UI_MODE_MIDI_LEARN) currentUIMode = UI_MODE_NONE;
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
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
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
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

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

	else return ACTION_RESULT_NOT_DEALT_WITH;

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
	if (oldItem == &multiRangeMenu) oldItem = multiRangeMenu.menuItemHeadingTo;

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
	    && getCurrentUI() != &sampleMarkerEditor && getCurrentUI() != &renameDrumUI)
		return false;

#if HAVE_OLED
	renderUIsForOled();
#endif

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD

	if (!inSettingsMenu() && currentItem != &sampleStartMenu && currentItem != &sampleEndMenu
	    && currentItem != &audioClipSampleMarkerEditorMenuStart && currentItem != &audioClipSampleMarkerEditorMenuEnd
	    && currentItem != &fileSelectorMenu && currentItem != &drumNameMenu) {

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

			if (currentItem == &multiRangeMenu) currentItem = multiRangeMenu.menuItemHeadingTo;

			// First, see if there's a shortcut for the actual MenuItem we're currently on
			for (int x = 0; x < 15; x++) {
				for (int y = 0; y < displayHeight; y++) {
					if (paramShortcutsForSounds[x][y] == currentItem) {

						if (x == 10 && y < 6 && editingReverbCompressor()) goto stopThat;

						if (currentParamShorcutX != 255 && (x & 1) && currentSourceIndex == 0) goto stopThat;
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

								if (currentParamShorcutX != 255 && (x & 1) && currentSourceIndex == 0) goto stopThat;

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
						if (source < NUM_PATCH_SOURCES)
							sourceShortcutBlinkFrequencies[x][y] = currentItem->shouldBlinkPatchingSourceShortcut(
							    source, &sourceShortcutBlinkColours[x][y]);
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
	    && currentUIMode != UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR)
		return;

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
		if (getCurrentMenuItem()->selectEncoderActionEditsInstrument())
			markInstrumentAsEdited(); // TODO: make reverb and reverb-compressor stuff exempt from this

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
	        && !(currentUIMode == UI_MODE_AUDITIONING && getRootUI() == &instrumentClipView)))
		return ACTION_RESULT_NOT_DEALT_WITH;

	if (on && isUIModeWithinRange(shortcutPadUIModes)) {

		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

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
					if (x == 11) item = midiOrCVParamShortcuts[y];
					else if (x == 4) {
						if (y == 7) item = &sequenceDirectionMenu;
					}
					else item = NULL;
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
				if (source == 254) numericDriver.displayPopup("SOON");

				if (source >= NUM_PATCH_SOURCES) return ACTION_RESULT_DEALT_WITH;

				bool previousPressStillActive = false;
				for (int h = 0; h < 2; h++) {
					for (int i = 0; i < displayHeight; i++) {
						if (h == 0 && i < 5) continue;

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
							if (!wentBack) numericDriver.setNextTransitionDirection(1);
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
	if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

	if (!inSettingsMenu()) {
		int result = potentialShortcutPadAction(x, y, on);
		if (result != ACTION_RESULT_NOT_DEALT_WITH) return result;
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
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(xEncButtonX, xEncButtonY))
		return ACTION_RESULT_DEALT_WITH;
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
	else UI::modEncoderAction(whichModEncoder, offset);
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
						if (noteRow == NULL) return false;
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

	if (item) newItem = (MenuItem*)item;
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

	MultiRange* newRange = currentMultiRange;

	if ((getCurrentUI() != &soundEditor && getCurrentUI() != &sampleMarkerEditor) || sourceIndex != currentSourceIndex)
		newRange = NULL;

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
		multiRangeMenu.menuItemHeadingTo = newItem;
		newItem = &multiRangeMenu;
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
                                                                    MultiRange** previouslySelectedRange) {

	Source* source = &sound->sources[whichThing];

	MultiRange* firstRange = source->getOrCreateFirstRange();
	if (!firstRange) {
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return MENU_PERMISSION_NO;
	}

	if (soundEditor.editingKit() || (automaticallySelectIfOnlyOne && source->ranges.getNumElements() == 1)) {
		*previouslySelectedRange = firstRange;
		return MENU_PERMISSION_YES;
	}

	if (getCurrentUI() == &soundEditor && *previouslySelectedRange && currentSourceIndex == whichThing)
		return MENU_PERMISSION_YES;

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

	else return currentMultiRange->getAudioFileHolder();
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
		if (currentMenuItem == &mpeZoneNumMemberChannelsMenu) {
			currentMenuItem->readValueAgain();
		}
	}
}

#if HAVE_OLED
void SoundEditor::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	// Sorry - extremely ugly hack here.
	MenuItem* currentMenuItem = getCurrentMenuItem();
	if (currentMenuItem == &drumNameMenu) {
		if (!navigationDepth) return;
		currentMenuItem = menuItemNavigationRecord[navigationDepth - 1];
	}

	currentMenuItem->renderOLED();
}
#endif

/*
char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
*/
