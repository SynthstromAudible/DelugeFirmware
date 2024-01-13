/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "storage/flash_storage.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/colour.h"
#include "gui/ui/sound_editor.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_engine.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "util/functions.h"
#include "util/misc.h"

extern "C" {
#include "RZA1/spibsc/r_spibsc_flash_api.h"
#include "RZA1/spibsc/spibsc.h"
}

#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"

using namespace deluge;

extern gui::menu_item::IntegerRange defaultTempoMenu;
extern gui::menu_item::IntegerRange defaultSwingMenu;
extern gui::menu_item::KeyRange defaultKeyMenu;

namespace FlashStorage {

/* Settings as stored in flash memory, byte by byte:

0: CurrentFirmware version; 0 if before V1.3.1; 0xFF if blank flash chip
1: (cancelled) RAM size. 0=32MB. 1=64MB
2: RAM size. 0=64MB. 1=32MB

10 - 12: CV mode (no longer used - next thing is, instead)
12 - 14: CV volts per octave (0 means use Hz per volt instead)
14 - 18: CV transpose
18 - 22: CV cents
22 - 30: gate types

30: gate off time
31: analog clock in auto start
32: analog clock in ppqn
33: analog clock out ppqn
34: MIDI out clock enabled
35: tempo magnitude matching enabled
36: cursor flash enabled
37: MIDI thru enabled
38: GlobalMIDICommand::PLAYBACK_RESTART channel + 1
39: GlobalMIDICommand::PLAYBACK_RESTART noteCode + 1
40: GlobalMIDICommand::PLAYBACK_PLAY channel + 1
41: GlobalMIDICommand::PLAYBACK_PLAY noteCode + 1
42: GlobalMIDICommand::PLAYBACK_RECORD channel + 1
43: GlobalMIDICommand::PLAYBACK_RECORD noteCode + 1
44: GlobalMIDICommand::PLAYBACK_TAP channel + 1
45: GlobalMIDICommand::PLAYBACK_TAP noteCode + 1
50: input monitoring mode
51: record quantize - 8
52: MIDI in clock enabled
53: default tempo min
54: default tempo max
55: default swing min
56: default swing max
57: default key min
58: default key max
59: default scale
60: shortcuts version
61: audioClipRecordMargins
62: count-in for recording
63: GlobalMIDICommand::LOOP channel + 1
64: GlobalMIDICommand::LOOP noteCode + 1
65: GlobalMIDICommand::UNDO channel + 1
66: GlobalMIDICommand::UNDO noteCode + 1
67: GlobalMIDICommand::REDO channel + 1
68: GlobalMIDICommand::REDO noteCode + 1
69: keyboard layout
70: GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING channel + 1
71: GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING noteCode + 1
72: sample browser preview mode
73: default velocity
74: "active" colour
75: "stopped" colour
76: "muted" colour
77: "solo" colour
78: default magnitude (resolution)
79: MIDI input device differentiation on/off
80-83: GlobalMIDICommand::PLAYBACK_RESTART				product / vendor ids
84-87: GlobalMIDICommand::PLAY							product / vendor ids
88-91: GlobalMIDICommand::RECORD						product / vendor ids
92-95: GlobalMIDICommand::TAP							product / vendor ids
96-99: GlobalMIDICommand::LOOP							product / vendor ids
100-103: GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING 	product / vendor ids
104-107: GlobalMIDICommand::UNDO						product / vendor ids
108-111: GlobalMIDICommand::REDO						product / vendor ids
112: default MIDI bend range
113: MIDI takeover mode
114: GlobalMIDICommand::FILL channel + 1
115: GlobalMIDICommand::FILL noteCode + 1
116-119: GlobalMIDICommand::FILL product / vendor ids
120: gridAllowGreenSelection
121: defaultGridActiveMode
122: defaultMetronomeVolume
123: defaultSessionLayout
124: defaultKeyboardLayout
125: gridUnarmEmptyPads
126: midiFollow set follow channel synth
127: midiFollow set follow channel kit
128: midiFollow set follow channel param
129: midiFollow set kit root note
130: midiFollow display param pop up
131: midiFollow feedback
132: midiFollow feedback automation mode
133: midiFollow feedback filter to handle feedback loops
134-137: midiFollow set follow device synth 	product / vendor ids
138-141: midiFollow set follow device kit		product / vendor ids
142-145: midiFollow set follow device param		product / vendor ids
*/

uint8_t defaultScale;
bool audioClipRecordMargins;
KeyboardLayout keyboardLayout;
uint8_t recordQuantizeLevel; // Assumes insideWorldTickMagnitude==1, which is not default anymore, so adjust accordingly
uint8_t sampleBrowserPreviewMode;
uint8_t defaultVelocity;
int8_t defaultMagnitude;

bool settingsBeenRead; // Whether the settings have been read from the flash chip yet
uint8_t ramSize;       // Deprecated

uint8_t defaultBendRange[2] = {
    2,
    48}; // The 48 isn't editable. And the 2 actually should only apply to non-MPE MIDI, because it's editable, whereas for MPE it's meant to always stay at 2.

SessionLayoutType defaultSessionLayout;
KeyboardLayoutType defaultKeyboardLayout;

bool gridUnarmEmptyPads;
bool gridAllowGreenSelection;
GridDefaultActiveMode defaultGridActiveMode;

uint8_t defaultMetronomeVolume;

void resetSettings() {

	cvEngine.setCVVoltsPerOctave(0, 100);
	cvEngine.setCVVoltsPerOctave(1, 100);

	cvEngine.setCVTranspose(0, 0, 0);
	cvEngine.setCVTranspose(1, 0, 0);

	for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {
		cvEngine.setGateType(i, GateType::V_TRIG);
	}

	cvEngine.minGateOffTime = 10;

	playbackHandler.analogClockInputAutoStart = true;
	playbackHandler.analogInTicksPPQN = 24;
	playbackHandler.analogOutTicksPPQN = 24;
	playbackHandler.midiOutClockEnabled = true;
	playbackHandler.midiInClockEnabled = true;
	playbackHandler.tempoMagnitudeMatchingEnabled = false;

	PadLEDs::flashCursor = FLASH_CURSOR_SLOW;

	midiEngine.midiThru = false;
	for (auto& midiChannelType : midiEngine.midiFollowChannelType) {
		midiChannelType.clear();
	}
	midiEngine.midiFollowKitRootNote = 36;
	midiEngine.midiFollowDisplayParam = false;
	midiEngine.midiFollowFeedback = false;
	midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
	midiEngine.midiFollowFeedbackFilter = false;

	midiEngine.midiTakeover = MIDITakeoverMode::JUMP;

	for (auto& globalMIDICommand : midiEngine.globalMIDICommands) {
		globalMIDICommand.clear();
	}

	AudioEngine::inputMonitoringMode = InputMonitoringMode::SMART;
	recordQuantizeLevel = 8;

	defaultTempoMenu.lower = 120;
	defaultTempoMenu.upper = 120;

	defaultSwingMenu.lower = 50;
	defaultSwingMenu.upper = 50;

	defaultKeyMenu.lower = 0;
	defaultKeyMenu.upper = 0;

	defaultScale = 0;

	soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);

	audioClipRecordMargins = true;
	playbackHandler.countInEnabled = false;
	keyboardLayout = KeyboardLayout::QWERTY;
	sampleBrowserPreviewMode = PREVIEW_ONLY_WHILE_NOT_PLAYING;

	defaultVelocity = 64;

	gui::menu_item::activeColourMenu.value = 1;  // Green
	gui::menu_item::stoppedColourMenu.value = 0; // Red
	gui::menu_item::mutedColourMenu.value = 3;   // Yellow
	gui::menu_item::soloColourMenu.value = 2;    // Blue

	defaultMagnitude = 2;

	MIDIDeviceManager::differentiatingInputsByDevice = false;

	defaultBendRange[BEND_RANGE_MAIN] = 2;

	defaultSessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	defaultKeyboardLayout = KeyboardLayoutType::KeyboardLayoutTypeIsomorphic;

	gridUnarmEmptyPads = false;
	gridAllowGreenSelection = true;
	defaultGridActiveMode = GridDefaultActiveModeSelection;

	defaultMetronomeVolume = kMaxMenuMetronomeVolumeValue;
}

void readSettings() {
	uint8_t* buffer = (uint8_t*)miscStringBuffer;
	R_SFLASH_ByteRead(0x80000 - 0x1000, buffer, 256, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT,
	                  SPIBSC_OUTPUT_ADDR_24);

	settingsBeenRead = true;

	int32_t previouslySavedByFirmwareVersion = buffer[0];

	// If no settings were previously saved, get out
	if (previouslySavedByFirmwareVersion == 0xFF) {
		resetSettings();
		return;
	}

	ramSize = buffer[2];

	cvEngine.setCVVoltsPerOctave(0, buffer[12]);
	cvEngine.setCVVoltsPerOctave(1, buffer[13]);

	cvEngine.setCVTranspose(0, buffer[14], buffer[18]);
	cvEngine.setCVTranspose(1, buffer[15], buffer[19]);

	for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {
		if (buffer[22 + i] >= kNumGateTypes) {
			cvEngine.setGateType(i, GateType::V_TRIG);
		}
		else {
			cvEngine.setGateType(i, static_cast<GateType>(buffer[22 + i]));
		}
	}

	cvEngine.minGateOffTime = buffer[30];

	playbackHandler.analogClockInputAutoStart = buffer[31];
	playbackHandler.analogInTicksPPQN = buffer[32];
	playbackHandler.analogOutTicksPPQN = buffer[33];
	playbackHandler.midiOutClockEnabled = buffer[34];
	playbackHandler.midiInClockEnabled = (previouslySavedByFirmwareVersion < FIRMWARE_2P1P0_BETA) ? true : buffer[52];
	playbackHandler.tempoMagnitudeMatchingEnabled = buffer[35];

	if (previouslySavedByFirmwareVersion < FIRMWARE_1P3P1) {
		PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
	}
	else {
		PadLEDs::flashCursor = buffer[36];
	}

	midiEngine.midiThru = buffer[37];

	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAYBACK_RESTART)].channelOrZone =
	    buffer[38] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAYBACK_RESTART)].noteOrCC = buffer[39] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)].channelOrZone = buffer[40] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)].noteOrCC = buffer[41] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::RECORD)].channelOrZone = buffer[42] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::RECORD)].noteOrCC = buffer[43] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TAP)].channelOrZone = buffer[44] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TAP)].noteOrCC = buffer[45] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP)].channelOrZone = buffer[63] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP)].noteOrCC = buffer[64] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::UNDO)].channelOrZone = buffer[65] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::UNDO)].noteOrCC = buffer[66] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::REDO)].channelOrZone = buffer[67] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::REDO)].noteOrCC = buffer[68] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING)].channelOrZone =
	    buffer[70] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING)].noteOrCC =
	    buffer[71] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::FILL)].channelOrZone = buffer[114] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::FILL)].noteOrCC = buffer[115] - 1;

	if (previouslySavedByFirmwareVersion >= FIRMWARE_3P2P0_ALPHA) {
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::PLAYBACK_RESTART, &buffer[80]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::PLAY, &buffer[84]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::RECORD, &buffer[88]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::TAP, &buffer[92]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::LOOP, &buffer[96]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING, &buffer[100]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::UNDO, &buffer[104]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::REDO, &buffer[108]);
		MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::FILL, &buffer[116]);
	}

	if (buffer[50] >= kNumInputMonitoringModes) {
		AudioEngine::inputMonitoringMode = InputMonitoringMode::SMART;
	}
	else {
		AudioEngine::inputMonitoringMode = static_cast<InputMonitoringMode>(buffer[50]);
	}

	recordQuantizeLevel = buffer[51] + 8;
	if (recordQuantizeLevel == 10) {
		recordQuantizeLevel = 8; // Since I've deprecated the ZOOM option
	}

	if (previouslySavedByFirmwareVersion < FIRMWARE_2P1P0_BETA || !buffer[53]) { // Remove the true!
		defaultTempoMenu.lower = 120;
		defaultTempoMenu.upper = 120;

		defaultSwingMenu.lower = 50;
		defaultSwingMenu.upper = 50;

		defaultKeyMenu.lower = 0;
		defaultKeyMenu.upper = 0;

		defaultScale = 0;
	}

	else {
		defaultTempoMenu.lower = buffer[53];
		defaultTempoMenu.upper = buffer[54];

		defaultSwingMenu.lower = buffer[55];
		defaultSwingMenu.upper = buffer[56];

		defaultKeyMenu.lower = buffer[57];
		defaultKeyMenu.upper = buffer[58];

		defaultScale = buffer[59];
	}

	soundEditor.setShortcutsVersion((previouslySavedByFirmwareVersion < FIRMWARE_2P1P3_BETA) ? SHORTCUTS_VERSION_1
	                                                                                         : buffer[60]);

	if (previouslySavedByFirmwareVersion < FIRMWARE_3P0P0_ALPHA) {
		audioClipRecordMargins = true;
		playbackHandler.countInEnabled = false;
		keyboardLayout = KeyboardLayout::QWERTY;
	}
	else {
		audioClipRecordMargins = buffer[61];
		playbackHandler.countInEnabled = buffer[62];
		if (buffer[69] >= kNumKeyboardLayouts) {
			keyboardLayout = KeyboardLayout::QWERTY;
		}
		else {
			keyboardLayout = static_cast<KeyboardLayout>(buffer[69]);
		}
	}

	if (previouslySavedByFirmwareVersion < FIRMWARE_3P0P0_BETA) {
		sampleBrowserPreviewMode = PREVIEW_ON;
	}
	else {
		sampleBrowserPreviewMode = buffer[72];
	}

	defaultVelocity = buffer[73];
	if (defaultVelocity >= 128 || defaultVelocity <= 0) {
		defaultVelocity = 64;
	}

	if (previouslySavedByFirmwareVersion < FIRMWARE_3P1P0_ALPHA) {
		gui::menu_item::activeColourMenu.value = 1;  // Green
		gui::menu_item::stoppedColourMenu.value = 0; // Red
		gui::menu_item::mutedColourMenu.value = 3;   // Yellow
		gui::menu_item::soloColourMenu.value = 2;    // Blue

		defaultMagnitude = 2;

		MIDIDeviceManager::differentiatingInputsByDevice = false;
	}
	else {
		gui::menu_item::activeColourMenu.value = buffer[74];
		gui::menu_item::stoppedColourMenu.value = buffer[75];
		gui::menu_item::mutedColourMenu.value = buffer[76];
		gui::menu_item::soloColourMenu.value = buffer[77];

		defaultMagnitude = buffer[78];

		MIDIDeviceManager::differentiatingInputsByDevice = buffer[79];

		if (previouslySavedByFirmwareVersion == FIRMWARE_3P1P0_ALPHA) { // Could surely delete this code?
			if (!gui::menu_item::activeColourMenu.value) {
				gui::menu_item::activeColourMenu.value = 1;
			}
			if (!gui::menu_item::mutedColourMenu.value) {
				gui::menu_item::mutedColourMenu.value = 3;
			}
			if (!gui::menu_item::soloColourMenu.value) {
				gui::menu_item::soloColourMenu.value = 2;
			}

			if (!defaultMagnitude) {
				defaultMagnitude = 2;
			}
		}
	}

	if (previouslySavedByFirmwareVersion < FIRMWARE_3P2P0_ALPHA) {
		defaultBendRange[BEND_RANGE_MAIN] = 12; // This was the old default
	}
	else {
		defaultBendRange[BEND_RANGE_MAIN] = buffer[112];
		if (!defaultBendRange[BEND_RANGE_MAIN]) {
			defaultBendRange[BEND_RANGE_MAIN] = 12;
		}
	}

	if (buffer[113] >= kNumMIDITakeoverModes) {
		midiEngine.midiTakeover = MIDITakeoverMode::JUMP;
	}
	else {
		midiEngine.midiTakeover = static_cast<MIDITakeoverMode>(buffer[113]);
	}

	// 114 and 115, and 116-119 used further up

	gridAllowGreenSelection = buffer[120];
	if (buffer[121] >= util::to_underlying(GridDefaultActiveModeMaxElement)) {
		defaultGridActiveMode = GridDefaultActiveMode::GridDefaultActiveModeSelection;
	}
	else {
		defaultGridActiveMode = static_cast<GridDefaultActiveMode>(buffer[121]);
	}

	defaultMetronomeVolume = buffer[122];
	if (defaultMetronomeVolume > kMaxMenuMetronomeVolumeValue
	    || defaultMetronomeVolume < kMinMenuMetronomeVolumeValue) {
		defaultMetronomeVolume = kMaxMenuMetronomeVolumeValue;
	}
	AudioEngine::metronome.setVolume(defaultMetronomeVolume);

	if (buffer[123] >= util::to_underlying(SessionLayoutTypeMaxElement)) {
		defaultSessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	}
	else {
		defaultSessionLayout = static_cast<SessionLayoutType>(buffer[123]);
	}

	if (buffer[124] >= util::to_underlying(KeyboardLayoutTypeMaxElement)) {
		defaultKeyboardLayout = KeyboardLayoutType::KeyboardLayoutTypeIsomorphic;
	}
	else {
		defaultKeyboardLayout = static_cast<KeyboardLayoutType>(buffer[124]);
	}

	gridUnarmEmptyPads = buffer[125];

	midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::SYNTH)].channelOrZone = buffer[126];
	midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone = buffer[127];
	midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::PARAM)].channelOrZone = buffer[128];
	midiEngine.midiFollowKitRootNote = buffer[129];
	midiEngine.midiFollowDisplayParam = buffer[130];
	midiEngine.midiFollowFeedback = buffer[131];

	if (buffer[132] > util::to_underlying(MIDIFollowFeedbackAutomationMode::HIGH)) {
		midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
	}
	else {
		midiEngine.midiFollowFeedbackAutomation = static_cast<MIDIFollowFeedbackAutomationMode>(buffer[132]);
	}

	midiEngine.midiFollowFeedbackFilter = !!buffer[133];

	MIDIDeviceManager::readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType::SYNTH, &buffer[134]);
	MIDIDeviceManager::readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType::KIT, &buffer[138]);
	MIDIDeviceManager::readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType::PARAM, &buffer[142]);
}

void writeSettings() {
	uint8_t* buffer = (uint8_t*)miscStringBuffer;
	memset(buffer, 0, kFilenameBufferSize);

	buffer[0] = kCurrentFirmwareVersion;

	buffer[2] = ramSize;

	buffer[12] = cvEngine.cvChannels[0].voltsPerOctave;
	buffer[13] = cvEngine.cvChannels[1].voltsPerOctave;

	buffer[14] = cvEngine.cvChannels[0].transpose;
	buffer[15] = cvEngine.cvChannels[1].transpose;

	buffer[18] = cvEngine.cvChannels[0].cents;
	buffer[19] = cvEngine.cvChannels[1].cents;

	for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {
		buffer[22 + i] = util::to_underlying(cvEngine.gateChannels[i].mode);
	}

	buffer[30] = cvEngine.minGateOffTime;
	buffer[31] = playbackHandler.analogClockInputAutoStart;
	buffer[32] = playbackHandler.analogInTicksPPQN;
	buffer[33] = playbackHandler.analogOutTicksPPQN;
	buffer[34] = playbackHandler.midiOutClockEnabled;
	buffer[52] = playbackHandler.midiInClockEnabled;
	buffer[35] = playbackHandler.tempoMagnitudeMatchingEnabled;
	buffer[36] = PadLEDs::flashCursor;
	buffer[37] = midiEngine.midiThru;
	buffer[38] =
	    midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAYBACK_RESTART)].channelOrZone + 1;
	buffer[39] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAYBACK_RESTART)].noteOrCC + 1;
	buffer[40] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)].channelOrZone + 1;
	buffer[41] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)].noteOrCC + 1;
	buffer[42] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::RECORD)].channelOrZone + 1;
	buffer[43] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::RECORD)].noteOrCC + 1;
	buffer[44] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TAP)].channelOrZone + 1;
	buffer[45] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TAP)].noteOrCC + 1;
	buffer[63] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP)].channelOrZone + 1;
	buffer[64] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP)].noteOrCC + 1;
	buffer[65] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::UNDO)].channelOrZone + 1;
	buffer[66] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::UNDO)].noteOrCC + 1;
	buffer[67] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::REDO)].channelOrZone + 1;
	buffer[68] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::REDO)].noteOrCC + 1;
	buffer[114] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::FILL)].channelOrZone + 1;
	buffer[115] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::FILL)].noteOrCC + 1;
	buffer[70] =
	    midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING)].channelOrZone
	    + 1;
	buffer[71] =
	    midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING)].noteOrCC + 1;

	/* Global MIDI command device references - these occupy 4 bytes each */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::PLAYBACK_RESTART, &buffer[80]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::PLAY, &buffer[84]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::RECORD, &buffer[88]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::TAP, &buffer[92]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::LOOP, &buffer[96]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING, &buffer[100]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::UNDO, &buffer[104]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::REDO, &buffer[108]);
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::FILL, &buffer[116]);

	buffer[50] = util::to_underlying(AudioEngine::inputMonitoringMode);

	buffer[51] = recordQuantizeLevel - 8;

	buffer[53] = defaultTempoMenu.lower;
	buffer[54] = defaultTempoMenu.upper;

	buffer[55] = defaultSwingMenu.lower;
	buffer[56] = defaultSwingMenu.upper;

	buffer[57] = defaultKeyMenu.lower;
	buffer[58] = defaultKeyMenu.upper;

	buffer[59] = defaultScale;
	buffer[60] = soundEditor.shortcutsVersion;

	buffer[61] = audioClipRecordMargins;
	buffer[62] = playbackHandler.countInEnabled;

	buffer[69] = util::to_underlying(keyboardLayout);
	buffer[72] = sampleBrowserPreviewMode;

	buffer[73] = defaultVelocity;

	buffer[74] = gui::menu_item::activeColourMenu.value;
	buffer[75] = gui::menu_item::stoppedColourMenu.value;
	buffer[76] = gui::menu_item::mutedColourMenu.value;
	buffer[77] = gui::menu_item::soloColourMenu.value;

	buffer[78] = defaultMagnitude;
	buffer[79] = MIDIDeviceManager::differentiatingInputsByDevice;

	buffer[112] = defaultBendRange[BEND_RANGE_MAIN];

	buffer[113] = util::to_underlying(midiEngine.midiTakeover);
	// 114, 115, and 116-119 used further up

	buffer[120] = gridAllowGreenSelection;
	buffer[121] = util::to_underlying(defaultGridActiveMode);

	buffer[122] = defaultMetronomeVolume;

	buffer[123] = util::to_underlying(defaultSessionLayout);
	buffer[124] = util::to_underlying(defaultKeyboardLayout);

	buffer[125] = gridUnarmEmptyPads;
	buffer[126] = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::SYNTH)].channelOrZone;
	buffer[127] = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone;
	buffer[128] = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::PARAM)].channelOrZone;
	buffer[129] = midiEngine.midiFollowKitRootNote;
	buffer[130] = midiEngine.midiFollowDisplayParam;
	buffer[131] = midiEngine.midiFollowFeedback;
	buffer[132] = util::to_underlying(midiEngine.midiFollowFeedbackAutomation);
	buffer[133] = midiEngine.midiFollowFeedbackFilter;
	MIDIDeviceManager::writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType::SYNTH, &buffer[134]);
	MIDIDeviceManager::writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType::KIT, &buffer[138]);
	MIDIDeviceManager::writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType::PARAM, &buffer[142]);

	R_SFLASH_EraseSector(0x80000 - 0x1000, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, 1, SPIBSC_OUTPUT_ADDR_24);
	R_SFLASH_ByteProgram(0x80000 - 0x1000, buffer, 256, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT,
	                     SPIBSC_OUTPUT_ADDR_24);
}

} // namespace FlashStorage
