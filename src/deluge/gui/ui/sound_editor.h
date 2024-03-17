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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "modulation/arpeggiator.h"

#define SHORTCUTS_VERSION_1 0
#define SHORTCUTS_VERSION_3 1
#define NUM_SHORTCUTS_VERSIONS 2

extern void enterEditor();
extern void enterSaveSynthPresetUI();

class Drum;
class Sound;
class Source;
class ParamManagerForTimeline;
class InstrumentClip;
class SideChain;
class Arpeggiator;
class MultisampleRange;
class Clip;
class SampleHolder;
class SampleControls;
class ModControllableAudio;
class ModelStackWithThreeMainThings;
class AudioFileHolder;
class MIDIDevice;
namespace deluge::gui::menu_item {
enum class RangeEdit : uint8_t;
}

class SoundEditor final : public UI {
public:
	SoundEditor();
	bool opened();
	void focusRegained();
	void displayOrLanguageChanged() final;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	Sound* currentSound;
	ModControllableAudio* currentModControllable;
	int8_t currentSourceIndex;
	Source* currentSource;
	ParamManagerForTimeline* currentParamManager;
	SideChain* currentSidechain;
	ArpeggiatorSettings* currentArpSettings;
	MultiRange* currentMultiRange;
	SampleControls* currentSampleControls;
	VoicePriority* currentPriority;
	int16_t currentMultiRangeIndex;
	MIDIDevice* currentMIDIDevice;
	deluge::gui::menu_item::RangeEdit editingRangeEdge;

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	ActionResult horizontalEncoderAction(int32_t offset);
	bool editingKit();

	ActionResult timerCallback() override;
	void setupShortcutBlink(int32_t x, int32_t y, int32_t frequency);
	bool findPatchedParam(int32_t paramLookingFor, int32_t* xout, int32_t* yout);
	void updateSourceBlinks(MenuItem* currentItem);

	int32_t menuCurrentScroll;

	uint8_t navigationDepth;
	uint8_t patchingParamSelected;
	uint8_t currentParamShorcutX;
	uint8_t currentParamShorcutY;
	uint8_t paramShortcutBlinkFrequency;
	uint8_t sourceShortcutBlinkFrequencies[2][kDisplayHeight];
	uint8_t sourceShortcutBlinkColours[2][kDisplayHeight];
	uint32_t shortcutBlinkCounter;

	uint32_t timeLastAttemptedAutomatedParamEdit;

	int8_t numberScrollAmount;
	uint32_t numberEditSize;
	int8_t numberEditPos;

	uint8_t shortcutsVersion;

	MenuItem* menuItemNavigationRecord[16];

	bool shouldGoUpOneLevelOnBegin;

	bool programChangeReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t program) { return false; }
	bool midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value);
	bool pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2);
	void selectEncoderAction(int8_t offset);
	bool canSeeViewUnderneath() { return true; }
	bool setup(Clip* clip = NULL, const MenuItem* item = NULL, int32_t sourceIndex = 0);
	void enterOrUpdateSoundEditor(bool on);
	void blinkShortcut();
	ActionResult potentialShortcutPadAction(int32_t x, int32_t y, bool on);
	bool editingReverbSidechain();
	MenuItem* getCurrentMenuItem();
	bool inSettingsMenu();
	bool inSongMenu();
	bool setupKitGlobalFXMenu;
	void exitCompletely();
	void goUpOneLevel();
	void enterSubmenu(MenuItem* newItem);
	bool pcReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t program);
	bool noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int32_t channel, int32_t note, int32_t velocity);
	void markInstrumentAsEdited();
	bool editingCVOrMIDIClip();
	bool isUntransposedNoteWithinRange(int32_t noteCode);
	void setCurrentMultiRange(int32_t i);
	void possibleChangeToCurrentRangeDisplay();
	MenuPermission checkPermissionToBeginSessionForRangeSpecificParam(Sound* sound, int32_t whichThing,
	                                                                  bool automaticallySelectIfOnlyOne,
	                                                                  MultiRange** previouslySelectedRange);
	void setupExclusiveShortcutBlink(int32_t x, int32_t y);
	void setShortcutsVersion(int32_t newVersion);
	ModelStackWithThreeMainThings* getCurrentModelStack(void* memory);

	void cutSound();
	AudioFileHolder* getCurrentAudioFileHolder();
	void mpeZonesPotentiallyUpdated();

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	// ui
	UIType getUIType() { return UIType::SOUND_EDITOR; }

private:
	bool beginScreen(MenuItem* oldMenuItem = NULL);
	uint8_t getActualParamFromScreen(uint8_t screen);
	void setLedStates();
};

extern SoundEditor soundEditor;
