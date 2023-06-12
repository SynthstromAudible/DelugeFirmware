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

#ifndef SOUNDEDITOR_H
#define SOUNDEDITOR_H

#include "UI.h"
#include "MenuItemSubmenu.h"
#include "Arpeggiator.h"

#define SHORTCUTS_VERSION_1 0
#define SHORTCUTS_VERSION_3 1
#define NUM_SHORTCUTS_VERSIONS 2

extern void enterEditor();
extern void enterSaveSynthPresetUI();

extern MenuItemSubmenu rootMenuSong;

class Drum;
class Sound;
class Source;
class ParamManagerForTimeline;
class InstrumentClip;
class Compressor;
class Arpeggiator;
class MultisampleRange;
class Clip;
class SampleHolder;
class SampleControls;
class ModControllableAudio;
class ModelStackWithThreeMainThings;
class AudioFileHolder;
class MIDIDevice;

class SoundEditor final : public UI {
public:
	SoundEditor();
	bool opened();
	void focusRegained();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	Sound* currentSound;
	ModControllableAudio* currentModControllable;
	int8_t currentSourceIndex;
	Source* currentSource;
	ParamManagerForTimeline* currentParamManager;
	Compressor* currentCompressor;
	ArpeggiatorSettings* currentArpSettings;
	MultiRange* currentMultiRange;
	SampleControls* currentSampleControls;
	uint8_t* currentPriority;
	int16_t currentMultiRangeIndex;
	MIDIDevice* currentMIDIDevice;
	uint8_t editingRangeEdge;

	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	int padAction(int x, int y, int velocity);
	int verticalEncoderAction(int offset, bool inCardRoutine);
	void modEncoderAction(int whichModEncoder, int offset);
	int horizontalEncoderAction(int offset);
	bool editingKit();

	void setupShortcutBlink(int x, int y, int frequency);

	int32_t currentValue;
	int menuCurrentScroll;

	uint8_t navigationDepth;
	uint8_t patchingParamSelected;
	uint8_t currentParamShorcutX;
	uint8_t currentParamShorcutY;
	uint8_t paramShortcutBlinkFrequency;
	uint8_t sourceShortcutBlinkFrequencies[2][displayHeight];
	uint8_t sourceShortcutBlinkColours[2][displayHeight];
	uint32_t shortcutBlinkCounter;

	uint32_t timeLastAttemptedAutomatedParamEdit;

	int8_t numberScrollAmount;
	uint32_t numberEditSize;
	int8_t numberEditPos;

	uint8_t shortcutsVersion;

	MenuItem* menuItemNavigationRecord[16];

	MenuItem** currentSubmenuItem;

	bool shouldGoUpOneLevelOnBegin;

	bool midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value);
	bool pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2);
	void selectEncoderAction(int8_t offset);
	bool canSeeViewUnderneath() { return true; }
	bool setup(Clip* clip = NULL, const MenuItem* item = NULL, int sourceIndex = 0);
	void blinkShortcut();
	int potentialShortcutPadAction(int x, int y, bool on);
	bool editingReverbCompressor();
	MenuItem* getCurrentMenuItem();
	bool inSettingsMenu();
	void exitCompletely();
	void goUpOneLevel();
	bool noteOnReceivedForMidiLearn(MIDIDevice* fromDevice, int channel, int note, int velocity);
	void markInstrumentAsEdited();
	bool editingCVOrMIDIClip();
	bool isUntransposedNoteWithinRange(int noteCode);
	void setCurrentMultiRange(int i);
	void possibleChangeToCurrentRangeDisplay();
	int checkPermissionToBeginSessionForRangeSpecificParam(Sound* sound, int whichThing,
	                                                       bool automaticallySelectIfOnlyOne,
	                                                       MultiRange** previouslySelectedRange);
	void setupExclusiveShortcutBlink(int x, int y);
	void setShortcutsVersion(int newVersion);
	ModelStackWithThreeMainThings* getCurrentModelStack(void* memory);

	void cutSound();
	AudioFileHolder* getCurrentAudioFileHolder();
	void mpeZonesPotentiallyUpdated();

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#endif

private:
	bool beginScreen(MenuItem* oldMenuItem = NULL);
	uint8_t getActualParamFromScreen(uint8_t screen);
	void setLedStates();
};

extern SoundEditor soundEditor;

#endif // SOUNDEDITOR_H
