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
#include "RZA1/cpu_specific.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/colour.h"
#include "gui/ui/sound_editor.h"
#include "hid/led/pad_leds.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "model/scale/preset_scales.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/misc.h"
#include <cstdint>

extern "C" {
#include "RZA1/spibsc/r_spibsc_flash_api.h"
#include "RZA1/spibsc/spibsc.h"
}

#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"

using namespace deluge;

extern gui::menu_item::IntegerRange defaultTempoMenu;
extern gui::menu_item::IntegerRange defaultSwingAmountMenu;
extern gui::menu_item::KeyRange defaultKeyMenu;

namespace FlashStorage {

// Static declarations
static bool areMidiFollowSettingsValid(std::span<uint8_t> buffer);
static bool areAutomationSettingsValid(std::span<uint8_t> buffer);

enum Entries {
	FIRMWARE_TYPE = 0,
	VERSION_MAJOR = 1,
	VERSION_MINOR = 2,
	VERSION_PATCH = 3,

	CV_MODE = 10,
	CV_VOLTS_PER_OCTAVE = 12,
	CV_TRANSPOSE = 14,
	CV_CENTS = 18,

	GATE_TYPE = 22,
};

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
59: default scale (deprecated, see slot 148)
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
125: gridEmptyPadsUnarm
126: midiFollow set follow channel A
127: midiFollow set follow channel B
128: midiFollow set follow channel C
129: midiFollow set kit root note
130: midiFollow display param pop up
131: midiFollow set feedback channel type (A/B/C/NONE)
132: midiFollow feedback automation mode
133: midiFollow feedback filter to handle feedback loops
134-137: midiFollow set follow device A product / vendor ids
138-141: midiFollow set follow device B	product / vendor ids
142-145: midiFollow set follow device C	product / vendor ids
146: gridEmptyPadsCreateRec
147: midi select kit row on learned note message received
148: default scale (NEW)
149: automationInterpolate;
150: automationClear;
151: automationShift;
152: automationNudgeNote;
153: automationDisableAuditionPadShortcuts;
154: keyboardFunctionsVelocityGlide;
155: keyboardFunctionsModwheelGlide;
156: MIDI Transpose ChannelOrZone
157: MIDI Transpose NoteOrCC
158-161: MIDI Transpose device / vendor ID
162: MIDI Transpose Control method.
163: default Startup Song Mode
164: default pad brightness
165: "fill" colour
166: "once" colour
167: defaultSliceMode
168: midiFollow control song params
169: High CPU Usage Indicator
170: default hold time (1-20)
171: default swing interval
172: default disabled scales low byte
173: default disabled scales high byte
174: accessibilityShortcuts
175: accessibilityMenuHighlighting
176: default new clip type
177: use last clip type
*/

uint8_t defaultScale;
bool audioClipRecordMargins;
KeyboardLayout keyboardLayout;
uint8_t recordQuantizeLevel; // Assumes insideWorldTickMagnitude==1, which is not default anymore, so adjust accordingly
uint8_t sampleBrowserPreviewMode;
uint8_t defaultVelocity;
int8_t defaultMagnitude;

bool settingsBeenRead; // Whether the settings have been read from the flash chip yet

uint8_t defaultBendRange[2] = {2, 48}; // The 48 isn't editable. And the 2 actually should only apply to non-MPE MIDI,
                                       // because it's editable, whereas for MPE it's meant to always stay at 2.

SessionLayoutType defaultSessionLayout;
KeyboardLayoutType defaultKeyboardLayout;

bool keyboardFunctionsVelocityGlide;
bool keyboardFunctionsModwheelGlide;

bool gridEmptyPadsUnarm;
bool gridEmptyPadsCreateRec;
bool gridAllowGreenSelection;
GridDefaultActiveMode defaultGridActiveMode;

SampleRepeatMode defaultSliceMode;

uint8_t defaultMetronomeVolume;
uint8_t defaultPadBrightness;

bool automationInterpolate = true;
bool automationClear = true;
bool automationShift = true;
bool automationNudgeNote = true;
bool automationDisableAuditionPadShortcuts = true;

StartupSongMode defaultStartupSongMode;

bool highCPUUsageIndicator;

uint8_t defaultHoldTime;
int32_t holdTime;

uint8_t defaultSwingInterval;

std::bitset<NUM_PRESET_SCALES> defaultDisabledPresetScales;
// We're storing the scales in a two-byte bitmask in the flash. Current intent is to not
// add any more builtin scales, but put all future scales on the SD card, which
// will have it's own disabled-flags. If we ever add more, we need to spend at least one byte
// more of flash.
static_assert(NUM_PRESET_SCALES <= 16);

bool accessibilityShortcuts = false;
bool accessibilityMenuHighlighting = true;

OutputType defaultNewClipType = OutputType::SYNTH;
bool defaultUseLastClipType = true;

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
	midiEngine.midiTakeover = MIDITakeoverMode::JUMP;
	midiEngine.midiSelectKitRow = false;

	resetMidiFollowSettings();

	for (auto& globalMIDICommand : midiEngine.globalMIDICommands) {
		globalMIDICommand.clear();
	}

	AudioEngine::inputMonitoringMode = InputMonitoringMode::SMART;
	recordQuantizeLevel = 8;

	defaultTempoMenu.lower = 120;
	defaultTempoMenu.upper = 120;

	defaultSwingAmountMenu.lower = 50;
	defaultSwingAmountMenu.upper = 50;

	defaultKeyMenu.lower = 0;
	defaultKeyMenu.upper = 0;

	defaultScale = 0;

	soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);

	audioClipRecordMargins = true;
	playbackHandler.countInBars = 0;
	keyboardLayout = KeyboardLayout::QWERTY;
	sampleBrowserPreviewMode = PREVIEW_ONLY_WHILE_NOT_PLAYING;

	defaultVelocity = 64;

	gui::menu_item::activeColourMenu.value = gui::menu_item::Colour::GREEN; // Green
	gui::menu_item::stoppedColourMenu.value = gui::menu_item::Colour::RED;  // Red
	gui::menu_item::mutedColourMenu.value = gui::menu_item::Colour::YELLOW; // Yellow
	gui::menu_item::soloColourMenu.value = gui::menu_item::Colour::BLUE;    // Blue

	gui::menu_item::fillColourMenu.value = gui::menu_item::Colour::AMBER;
	gui::menu_item::onceColourMenu.value = gui::menu_item::Colour::MAGENTA;

	defaultMagnitude = 2;

	MIDIDeviceManager::differentiatingInputsByDevice = false;

	defaultBendRange[BEND_RANGE_MAIN] = 2;

	defaultSessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	defaultKeyboardLayout = KeyboardLayoutType::KeyboardLayoutTypeIsomorphic;

	gridEmptyPadsUnarm = false;
	gridEmptyPadsCreateRec = false;
	gridAllowGreenSelection = true;
	defaultGridActiveMode = GridDefaultActiveModeSelection;

	defaultMetronomeVolume = kMaxMenuMetronomeVolumeValue;

	resetAutomationSettings();

	defaultStartupSongMode = StartupSongMode::BLANK;

	defaultSliceMode = SampleRepeatMode::CUT;

	highCPUUsageIndicator = false;

	defaultHoldTime = 2;
	holdTime = (defaultHoldTime * kSampleRate) / 20;

	defaultSwingInterval = 8 - defaultMagnitude; // 16th notes

	defaultDisabledPresetScales = {0};

	accessibilityShortcuts = false;
	accessibilityMenuHighlighting = true;

	defaultNewClipType = OutputType::SYNTH;
	defaultUseLastClipType = true;
}

void resetMidiFollowSettings() {
	for (auto& midiChannelType : midiEngine.midiFollowChannelType) {
		midiChannelType.clear();
	}
	midiEngine.midiFollowKitRootNote = 36;
	midiEngine.midiFollowDisplayParam = false;
	midiEngine.midiFollowFeedbackChannelType = MIDIFollowChannelType::NONE;
	midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
	midiEngine.midiFollowFeedbackFilter = false;
}

void resetAutomationSettings() {
	automationInterpolate = true;
	automationClear = true;
	automationShift = true;
	automationNudgeNote = true;
	automationDisableAuditionPadShortcuts = true;
}

void readSettings() {
	std::span buffer{(uint8_t*)miscStringBuffer, kFilenameBufferSize};
	R_SFLASH_ByteRead(0x80000 - 0x1000, buffer.data(), kFilenameBufferSize, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE,
	                  SPIBSC_1BIT, SPIBSC_OUTPUT_ADDR_24);

	settingsBeenRead = true;

	FirmwareVersion::Type firmwareType{buffer[FIRMWARE_TYPE]};

	// If no settings were previously saved, get out
	if (firmwareType == FirmwareVersion::Type::UNKNOWN) {
		resetSettings();
		return;
	}

	FirmwareVersion savedVersion = (firmwareType != FirmwareVersion::Type::COMMUNITY)
	                                   ? FirmwareVersion(firmwareType, {0, 0, 0})
	                                   : FirmwareVersion::community({
	                                       .major = buffer[VERSION_MAJOR],
	                                       .minor = buffer[VERSION_MINOR],
	                                       .patch = buffer[VERSION_PATCH],
	                                   });

	for (int chan = 0; chan < NUM_CV_CHANNELS; ++chan) {
		cvEngine.setCVVoltsPerOctave(chan, buffer[CV_VOLTS_PER_OCTAVE + chan]);
		cvEngine.setCVTranspose(chan, buffer[CV_TRANSPOSE + chan], buffer[CV_CENTS + chan]);
	}

	for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {
		if (buffer[GATE_TYPE + i] >= kNumGateTypes) {
			cvEngine.setGateType(i, GateType::V_TRIG);
		}
		else {
			cvEngine.setGateType(i, GateType{buffer[GATE_TYPE + i]});
		}
	}

	cvEngine.minGateOffTime = buffer[30];

	playbackHandler.analogClockInputAutoStart = buffer[31];
	playbackHandler.analogInTicksPPQN = buffer[32];
	playbackHandler.analogOutTicksPPQN = buffer[33];
	playbackHandler.midiOutClockEnabled = buffer[34];
	playbackHandler.midiInClockEnabled = buffer[52];
	playbackHandler.tempoMagnitudeMatchingEnabled = buffer[35];

	PadLEDs::flashCursor = buffer[36];

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

	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::PLAYBACK_RESTART, &buffer[80]);
	/* buffer[81]  \
	   buffer[82]   device reference above occupies 4 bytes
	   buffer[83] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::PLAY, &buffer[84]);
	/* buffer[85]  \
	   buffer[86]   device reference above occupies 4 bytes
	   buffer[87] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::RECORD, &buffer[88]);
	/* buffer[89]  \
	   buffer[90]   device reference above occupies 4 bytes
	   buffer[91] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::TAP, &buffer[92]);
	/* buffer[93]  \
	   buffer[94]   device reference above occupies 4 bytes
	   buffer[95] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::LOOP, &buffer[96]);
	/* buffer[97]  \
	   buffer[98]   device reference above occupies 4 bytes
	   buffer[99] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING, &buffer[100]);
	/* buffer[101]  \
	   buffer[102]   device reference above occupies 4 bytes
	   buffer[103] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::UNDO, &buffer[104]);
	/* buffer[105]  \
	   buffer[106]   device reference above occupies 4 bytes
	   buffer[107] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::REDO, &buffer[108]);
	/* buffer[109]  \
	   buffer[110]   device reference above occupies 4 bytes
	   buffer[111] */
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::FILL, &buffer[116]);
	/* buffer[117]  \
	   buffer[118]   device reference above occupies 4 bytes
	   buffer[119] */

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

	if (buffer[53] == 0u) { // Remove the true!
		defaultTempoMenu.lower = 120;
		defaultTempoMenu.upper = 120;

		defaultSwingAmountMenu.lower = 50;
		defaultSwingAmountMenu.upper = 50;

		defaultKeyMenu.lower = 0;
		defaultKeyMenu.upper = 0;

		defaultScale = 0;
	}

	else {
		defaultTempoMenu.lower = buffer[53];
		defaultTempoMenu.upper = buffer[54];

		defaultSwingAmountMenu.lower = buffer[55];
		defaultSwingAmountMenu.upper = buffer[56];

		defaultKeyMenu.lower = buffer[57];
		defaultKeyMenu.upper = buffer[58];

		if (buffer[59] == OFFICIAL_FIRMWARE_RANDOM_SCALE_INDEX) {
			// If the old value was set to RANDOM,
			// import it adapting to the new RANDOM index
			defaultScale = RANDOM_SCALE;
		}
		else if (buffer[59] == OFFICIAL_FIRMWARE_NONE_SCALE_INDEX) {
			// If the old value is "NONE"
			// we have already imported the old value,
			// so we can directly load the new one
			defaultScale = buffer[148];
		}
		else {
			// If the old value is between 0 and 6 (Major to Locrian),
			// import the old scale
			defaultScale = buffer[59];
		}
	}

	soundEditor.setShortcutsVersion(buffer[60]);
	audioClipRecordMargins = buffer[61];
	playbackHandler.countInBars = buffer[62];
	keyboardLayout =
	    (buffer[69] >= kNumKeyboardLayouts) ? KeyboardLayout::QWERTY : static_cast<KeyboardLayout>(buffer[69]);

	sampleBrowserPreviewMode = buffer[72];

	defaultVelocity = buffer[73];
	if (defaultVelocity >= 128 || defaultVelocity <= 0) {
		defaultVelocity = 64;
	}

	gui::menu_item::activeColourMenu.value = static_cast<gui::menu_item::Colour::Option>(buffer[74]);
	gui::menu_item::stoppedColourMenu.value = static_cast<gui::menu_item::Colour::Option>(buffer[75]);
	gui::menu_item::mutedColourMenu.value = static_cast<gui::menu_item::Colour::Option>(buffer[76]);
	gui::menu_item::soloColourMenu.value = static_cast<gui::menu_item::Colour::Option>(buffer[77]);

	defaultMagnitude = buffer[78];

	MIDIDeviceManager::differentiatingInputsByDevice = buffer[79];

	defaultBendRange[BEND_RANGE_MAIN] = buffer[112];
	if (!defaultBendRange[BEND_RANGE_MAIN]) {
		defaultBendRange[BEND_RANGE_MAIN] = 12;
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

	gridEmptyPadsUnarm = buffer[125];

	if (savedVersion.type() != FirmwareVersion::Type::COMMUNITY) {
		resetMidiFollowSettings();
	}
	else {
		if (areMidiFollowSettingsValid(buffer)) {
			midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::A)].channelOrZone = buffer[126];
			midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::B)].channelOrZone = buffer[127];
			midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::C)].channelOrZone = buffer[128];
			MIDIDeviceManager::readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType::A, &buffer[134]);
			/* buffer[135]  \
			buffer[136]   device reference above occupies 4 bytes
			buffer[137] */
			MIDIDeviceManager::readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType::B, &buffer[138]);
			/* buffer[139]  \
			buffer[140]   device reference above occupies 4 bytes
			buffer[141] */
			MIDIDeviceManager::readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType::C, &buffer[142]);
			/* buffer[143]  \
			buffer[144]   device reference above occupies 4 bytes
			buffer[145] */
			midiEngine.midiFollowKitRootNote = buffer[129];
			midiEngine.midiFollowDisplayParam = !!buffer[130];
			midiEngine.midiFollowFeedbackChannelType = static_cast<MIDIFollowChannelType>(buffer[131]);
			midiEngine.midiFollowFeedbackAutomation = static_cast<MIDIFollowFeedbackAutomationMode>(buffer[132]);
			midiEngine.midiFollowFeedbackFilter = !!buffer[133];
		}
		else {
			resetMidiFollowSettings();
		}
	}

	gridEmptyPadsCreateRec = buffer[146];

	midiEngine.midiSelectKitRow = buffer[147];

	if (savedVersion.type() != FirmwareVersion::Type::COMMUNITY) {
		resetAutomationSettings();
	}
	else {
		if (areAutomationSettingsValid(buffer)) {
			automationInterpolate = buffer[149];
			automationClear = buffer[150];
			automationShift = buffer[151];
			automationNudgeNote = buffer[152];
			automationDisableAuditionPadShortcuts = buffer[153];
		}
		else {
			resetAutomationSettings();
		}
	}

	keyboardFunctionsVelocityGlide = buffer[154];
	keyboardFunctionsModwheelGlide = buffer[155];

	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TRANSPOSE)].channelOrZone = buffer[156] - 1;
	midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TRANSPOSE)].noteOrCC = buffer[157] - 1;
	MIDIDeviceManager::readDeviceReferenceFromFlash(GlobalMIDICommand::TRANSPOSE, &buffer[158]);
	/* buffer[159]  \
	   buffer[160]   device reference above occupies 4 bytes
	   buffer[161] */

	if (buffer[162] >= kNumMIDITransposeControlMethods) {
		MIDITranspose::controlMethod = MIDITransposeControlMethod::INKEY;
	}
	else {
		MIDITranspose::controlMethod = static_cast<MIDITransposeControlMethod>(buffer[162]);
	}

	if (buffer[163] >= kNumStartupSongMode) {
		defaultStartupSongMode = StartupSongMode::BLANK;
	}
	else {
		defaultStartupSongMode = static_cast<StartupSongMode>(buffer[163]);
	}

	defaultPadBrightness = buffer[164] == 0 ? kMaxLedBrightness : buffer[164];

	if (buffer[165] >= gui::menu_item::kNumPadColours) {
		gui::menu_item::fillColourMenu.value = gui::menu_item::Colour::AMBER;
	}
	else {
		gui::menu_item::fillColourMenu.value = static_cast<gui::menu_item::Colour::Option>(buffer[165]);
	}
	if (buffer[166] >= gui::menu_item::kNumPadColours) {
		gui::menu_item::fillColourMenu.value = gui::menu_item::Colour::MAGENTA;
	}
	else {
		gui::menu_item::fillColourMenu.value = static_cast<gui::menu_item::Colour::Option>(buffer[166]);
	}
	if (gui::menu_item::fillColourMenu.value == gui::menu_item::Colour::RED
	    && gui::menu_item::onceColourMenu.value == gui::menu_item::Colour::RED) {
		// Reset to default if both red, as they will be first time.
		gui::menu_item::fillColourMenu.value = gui::menu_item::Colour::AMBER;
		gui::menu_item::onceColourMenu.value = gui::menu_item::Colour::MAGENTA;
	}

	if (buffer[167] >= kNumRepeatModes) {
		defaultSliceMode = SampleRepeatMode::CUT;
	}
	else {
		defaultSliceMode = static_cast<SampleRepeatMode>(buffer[167]);
	}

	if (buffer[169] != 0 && buffer[169] != 1) {
		highCPUUsageIndicator = false;
	}
	else {
		highCPUUsageIndicator = buffer[169];
	}

	defaultHoldTime = buffer[170];
	if (defaultHoldTime > 20 || defaultHoldTime <= 0) {
		defaultHoldTime = 2;
	}
	holdTime = (defaultHoldTime * kSampleRate) / 20;

	defaultSwingInterval = buffer[171];
	if (defaultSwingInterval < MIN_SWING_INERVAL || MAX_SWING_INTERVAL < defaultSwingInterval) {
		defaultSwingInterval = 8 - defaultMagnitude; // 16th notes
	}

	if (savedVersion < FirmwareVersion::community({1, 2, 0})) {
		defaultDisabledPresetScales = std::bitset<NUM_PRESET_SCALES>(0);
	}
	else {
		defaultDisabledPresetScales = std::bitset<NUM_PRESET_SCALES>((buffer[173] << 8) | buffer[172]);
	}

	if (buffer[174] != 0 && buffer[174] != 1) {
		accessibilityShortcuts = false;
	}
	else {
		accessibilityShortcuts = buffer[174];
	}

	if (buffer[175] != 0 && buffer[175] != 1) {
		accessibilityMenuHighlighting = false;
	}
	else {
		accessibilityMenuHighlighting = buffer[175];
	}

	if (buffer[176] < 0 && buffer[176] > util::to_underlying(OutputType::AUDIO)) {
		defaultNewClipType = OutputType::SYNTH;
	}
	else {
		defaultNewClipType = static_cast<OutputType>(buffer[176]);
	}

	if (buffer[177] != 0 && buffer[177] != 1) {
		defaultUseLastClipType = true;
	}
	else {
		defaultUseLastClipType = buffer[177];
	}

}

static bool areMidiFollowSettingsValid(std::span<uint8_t> buffer) {
	// midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::A)].channelOrZone
	if ((buffer[126] < 0 || buffer[126] >= NUM_CHANNELS) && buffer[126] != MIDI_CHANNEL_NONE) {
		return false;
	}
	// midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::B)].channelOrZone
	else if ((buffer[127] < 0 || buffer[127] >= NUM_CHANNELS) && buffer[127] != MIDI_CHANNEL_NONE) {
		return false;
	}
	// midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::C)].channelOrZone
	else if ((buffer[128] < 0 || buffer[128] >= NUM_CHANNELS) && buffer[128] != MIDI_CHANNEL_NONE) {
		return false;
	}
	// midiEngine.midiFollowKitRootNote
	else if (buffer[129] < 0 || buffer[129] > kMaxMIDIValue) {
		return false;
	}
	// midiEngine.midiFollowDisplayParam
	else if (buffer[130] != 0 && buffer[130] != 1) {
		return false;
	}
	// midiEngine.midiFollowFeedbackChannelType
	else if (buffer[131] < 0 || buffer[131] > util::to_underlying(MIDIFollowChannelType::NONE)) {
		return false;
	}
	// midiEngine.midiFollowFeedbackAutomation
	else if (buffer[132] < 0 || buffer[132] > util::to_underlying(MIDIFollowFeedbackAutomationMode::HIGH)) {
		return false;
	}
	// midiEngine.midiFollowFeedbackFilter
	else if (buffer[133] != 0 && buffer[133] != 1) {
		return false;
	}
	// place holder for checking if midi follow devices are valid
	return true;
}

static bool areAutomationSettingsValid(std::span<uint8_t> buffer) {
	// automationInterpolate
	if (buffer[149] != 0 && buffer[149] != 1) {
		return false;
	}
	// automationClear
	else if (buffer[150] != 0 && buffer[150] != 1) {
		return false;
	}
	// automationShift
	else if (buffer[151] != 0 && buffer[151] != 1) {
		return false;
	}
	// automationNudgeNote
	else if (buffer[152] != 0 && buffer[152] != 1) {
		return false;
	}
	// automationDisableAuditionPadShortcuts
	else if (buffer[153] != 0 && buffer[153] != 1) {
		return false;
	}

	return true;
}

void writeSettings() {
	std::span<uint8_t> buffer{(uint8_t*)miscStringBuffer, kFilenameBufferSize};
	std::fill(buffer.begin(), buffer.end(), 0);

	buffer[FIRMWARE_TYPE] = util::to_underlying(FirmwareVersion::current().type());
	buffer[VERSION_MAJOR] = FirmwareVersion::current().version().major;
	buffer[VERSION_MINOR] = FirmwareVersion::current().version().minor;
	buffer[VERSION_PATCH] = FirmwareVersion::current().version().patch;

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
	/* buffer[81]  \
	   buffer[82]   device reference above occupies 4 bytes
	   buffer[83] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::PLAY, &buffer[84]);
	/* buffer[85]  \
	   buffer[86]   device reference above occupies 4 bytes
	   buffer[87] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::RECORD, &buffer[88]);
	/* buffer[89]  \
	   buffer[90]   device reference above occupies 4 bytes
	   buffer[91] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::TAP, &buffer[92]);
	/* buffer[93]  \
	   buffer[94]   device reference above occupies 4 bytes
	   buffer[95] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::LOOP, &buffer[96]);
	/* buffer[97]  \
	   buffer[98]   device reference above occupies 4 bytes
	   buffer[99] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING, &buffer[100]);
	/* buffer[101]  \
	   buffer[102]   device reference above occupies 4 bytes
	   buffer[103] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::UNDO, &buffer[104]);
	/* buffer[105]  \
	   buffer[106]   device reference above occupies 4 bytes
	   buffer[107] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::REDO, &buffer[108]);
	/* buffer[109]  \
	   buffer[110]   device reference above occupies 4 bytes
	   buffer[111] */
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::FILL, &buffer[116]);
	/* buffer[117]  \
	   buffer[118]   device reference above occupies 4 bytes
	   buffer[119] */

	buffer[50] = util::to_underlying(AudioEngine::inputMonitoringMode);

	buffer[51] = recordQuantizeLevel - 8;

	buffer[53] = defaultTempoMenu.lower;
	buffer[54] = defaultTempoMenu.upper;

	buffer[55] = defaultSwingAmountMenu.lower;
	buffer[56] = defaultSwingAmountMenu.upper;

	buffer[57] = defaultKeyMenu.lower;
	buffer[58] = defaultKeyMenu.upper;

	buffer[59] = OFFICIAL_FIRMWARE_NONE_SCALE_INDEX; // tombstone value for official firmware Default Scale slot
	buffer[148] = defaultScale;
	buffer[60] = soundEditor.shortcutsVersion;

	buffer[61] = audioClipRecordMargins;
	buffer[62] = playbackHandler.countInBars;

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

	buffer[125] = gridEmptyPadsUnarm;
	buffer[126] = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::A)].channelOrZone;
	buffer[127] = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::B)].channelOrZone;
	buffer[128] = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::C)].channelOrZone;
	MIDIDeviceManager::writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType::A, &buffer[134]);
	/* buffer[135]  \
	   buffer[136]   device reference above occupies 4 bytes
	   buffer[137] */
	MIDIDeviceManager::writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType::B, &buffer[138]);
	/* buffer[139]  \
	   buffer[140]   device reference above occupies 4 bytes
	   buffer[141] */
	MIDIDeviceManager::writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType::C, &buffer[142]);
	/* buffer[143]  \
	   buffer[144]   device reference above occupies 4 bytes
	   buffer[145] */
	buffer[129] = midiEngine.midiFollowKitRootNote;
	buffer[130] = midiEngine.midiFollowDisplayParam;
	buffer[131] = util::to_underlying(midiEngine.midiFollowFeedbackChannelType);
	buffer[132] = util::to_underlying(midiEngine.midiFollowFeedbackAutomation);
	buffer[133] = midiEngine.midiFollowFeedbackFilter;

	buffer[146] = gridEmptyPadsCreateRec;

	buffer[147] = midiEngine.midiSelectKitRow;

	buffer[149] = automationInterpolate;
	buffer[150] = automationClear;
	buffer[151] = automationShift;
	buffer[152] = automationNudgeNote;
	buffer[153] = automationDisableAuditionPadShortcuts;

	buffer[154] = keyboardFunctionsVelocityGlide;
	buffer[155] = keyboardFunctionsModwheelGlide;

	buffer[156] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TRANSPOSE)].channelOrZone + 1;
	buffer[157] = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TRANSPOSE)].noteOrCC + 1;
	MIDIDeviceManager::writeDeviceReferenceToFlash(GlobalMIDICommand::TRANSPOSE, &buffer[158]);
	/* buffer[159]  \
	   buffer[160]   device reference above occupies 4 bytes
	   buffer[161] */
	buffer[162] = util::to_underlying(MIDITranspose::controlMethod);

	buffer[163] = util::to_underlying(defaultStartupSongMode);
	buffer[164] = defaultPadBrightness;

	buffer[165] = gui::menu_item::fillColourMenu.value;
	buffer[166] = gui::menu_item::onceColourMenu.value;

	buffer[167] = util::to_underlying(defaultSliceMode);

	buffer[169] = highCPUUsageIndicator;

	buffer[170] = defaultHoldTime;

	buffer[171] = defaultSwingInterval;

	unsigned long disabledBits = defaultDisabledPresetScales.to_ulong();
	buffer[172] = 0xff & disabledBits;
	buffer[173] = 0xff & (disabledBits >> 8);

	buffer[174] = accessibilityShortcuts;
	buffer[175] = accessibilityMenuHighlighting;
	buffer[176] = util::to_underlying(defaultNewClipType);
	buffer[177] = defaultUseLastClipType;

	R_SFLASH_EraseSector(0x80000 - 0x1000, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, 1, SPIBSC_OUTPUT_ADDR_24);
	R_SFLASH_ByteProgram(0x80000 - 0x1000, buffer.data(), 256, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT,
	                     SPIBSC_OUTPUT_ADDR_24);
}

} // namespace FlashStorage
