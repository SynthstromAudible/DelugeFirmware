/*
 * Copyright (c) 2023 Aria Burrell (litui)
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

#include "io/midi/device_specific/midi_device_lumi_keys.h"
#include "gui/ui/ui.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

// Determine whether the current device is a Lumi Keys
bool MIDIDeviceLumiKeys::matchesVendorProduct(uint16_t vendorId, uint16_t productId) {
	for (uint8_t pair = 0; pair < MIDI_DEVICE_LUMI_KEYS_VP_COUNT; pair++) {
		if (lumiKeysVendorProductPairs[pair][0] == vendorId && lumiKeysVendorProductPairs[pair][1] == productId) {
			return true;
		}
	}

	return false;
}

void MIDIDeviceLumiKeys::hookOnConnected() {
	uint8_t upperZoneLastChannel = this->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeUpperZoneLastMemberChannel;
	uint8_t lowerZoneLastChannel = this->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel;

	RootNote currentRoot = RootNote(currentSong->rootNote % kOctaveSize);
	Scale currentScale = determineScaleFromNotes(currentSong->modeNotes, currentSong->numModeNotes);

	if (lowerZoneLastChannel != 0 || upperZoneLastChannel != 15) {
		setMIDIMode(MIDIMode::MPE);

		// Lumi Keys seems to only allow either Lower or Upper modes
		// We'll default to Lower mode for now unless Upper is enabled.

		if (upperZoneLastChannel != 15) {
			setMPEZone(MPEZone::UPPER);
		}
		else {
			setMPEZone(MPEZone::LOWER);
		}

		setMPENumChannels(15); // Max out to 15 since we can't do split config.
	}
	else {
		// Fall back to single-channel mode if MPE is off
		setMIDIMode(MIDIMode::SINGLE);
	}

	// Since we're in the neighbourhood, set the root and scale
	setRootNote(currentRoot);
	setScale(currentScale);

	// Run colour-setting hook.
	hookOnRecalculateColour();
}

void MIDIDeviceLumiKeys::hookOnWriteHostedDeviceToFile() {
	// Just call hookOnConnected as the same logic applies.
	hookOnConnected();
}

void MIDIDeviceLumiKeys::hookOnChangeRootNote() {
	setRootNote(RootNote(currentSong->rootNote % kOctaveSize));
}

void MIDIDeviceLumiKeys::hookOnChangeScale() {
	Scale scale = determineScaleFromNotes(currentSong->modeNotes, currentSong->numModeNotes);
	setScale(scale);
}

void MIDIDeviceLumiKeys::hookOnEnterScaleMode() {
	hookOnChangeRootNote();
	hookOnChangeScale();
}

void MIDIDeviceLumiKeys::hookOnExitScaleMode() {
	hookOnChangeRootNote();
	setScale(Scale::CHROMATIC);
}

void MIDIDeviceLumiKeys::hookOnMIDILearn() {
	hookOnRecalculateColour();
}

void MIDIDeviceLumiKeys::hookOnTransitionToSessionView() {
	hookOnRecalculateColour();
}

void MIDIDeviceLumiKeys::hookOnTransitionToClipView() {
	// recalculate colour already runs on this transition
	// hookOnRecalculateColour();
}

void MIDIDeviceLumiKeys::hookOnTransitionToArrangerView() {
	hookOnRecalculateColour();
}

void MIDIDeviceLumiKeys::hookOnRecalculateColour() {
	InstrumentClip* clip = getCurrentInstrumentClip();

	bool learnedToCurrentClip = false;

	if (clip) {
		// Determine if learned device on the current clip is the same as this one
		LearnedMIDI* midiInput = &(((MelodicInstrument*)clip->output)->midiInput);
		if (midiInput->containsSomething() && midiInput->device) {
			if (midiInput->device->getDisplayName() == getDisplayName()
			    && midiInput->device->connectionFlags == connectionFlags) {
				learnedToCurrentClip = true;
			}
		}

		bool applicableUIMode = isUIModeActive(UI_MODE_NONE) || isUIModeActive(UI_MODE_MIDI_LEARN)
		                        || isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW);

		if (learnedToCurrentClip && applicableUIMode) {
			uint32_t yPos = 0;
			uint32_t offset = 0;
			NoteRow* noteRow = clip->getNoteRowOnScreen(yPos, currentSong);

			if (noteRow) {
				offset = noteRow->getColourOffset(clip);
			}

			RGB rgb_main;
			// uint8_t rgb_tail[3];
			RGB rgb_blur;

			rgb_main = clip->getMainColourFromY(clip->getYNoteFromYDisplay(yPos, currentSong), offset);
			// getTailColour(rgb_tail, rgb_main);
			rgb_blur = rgb_main.forBlur();

			setColour(ColourZone::ROOT, rgb_main);
			setColour(ColourZone::GLOBAL, rgb_blur);

			return;
		}
	}

	setColour(ColourZone::ROOT, deluge::gui::colours::black);
	setColour(ColourZone::GLOBAL, deluge::gui::colours::black);
}

// Private functions

uint8_t MIDIDeviceLumiKeys::sysexChecksum(uint8_t* chkBytes, uint8_t size) {
	uint8_t c = size;
	for (uint8_t i = 0; i < size; i++) {
		c = (c * 3 + chkBytes[i]) & 0xff;
	}
	return c & 0x7f;
}

void MIDIDeviceLumiKeys::sendLumiCommand(uint8_t* command, uint8_t length) {
	uint8_t sysexMsg[16] = {0};
	sysexMsg[0] = MIDI_DEVICE_LUMI_KEYS_SYSEX_START;
	memcpy(&sysexMsg[1], sysexManufacturer, 3);
	sysexMsg[4] = MIDI_DEVICE_LUMI_KEYS_SYSEX_SPACER;
	sysexMsg[5] = MIDI_DEVICE_LUMI_KEYS_DEVICE;
	memcpy(&sysexMsg[6], command, length);
	sysexMsg[14] = sysexChecksum(&sysexMsg[6], 8);
	sysexMsg[15] = MIDI_DEVICE_LUMI_KEYS_SYSEX_END;

	sendSysex(sysexMsg, 16);
}

/// @brief Fills the first 6 values at the pointer location with a 7-bit, offset representation of the 32-bit signed
/// index
/// @param destination A pointer with space for 6 bytes
/// @param index The index of the value we want to generate
/// @param value_offset The offset for the resultant values (eg: 0x00 + value_offset, 0x20 + value_offset, etc.)
void MIDIDeviceLumiKeys::getCounterCodes(uint8_t* destination, int32_t index, uint8_t valueOffset) {
	destination[0] = (index % (uint32_t)4 * 32) + valueOffset;
	int32_t tens = index / (uint32_t)4;
	destination[1] = tens & 0x7f;
	destination[2] = (tens >> 7) & 0x7f;
	destination[3] = (tens >> 14) & 0x7f;
	destination[4] = (tens >> 21) & 0x7f;
	destination[5] = (tens >> 28) & 0x7f;
}

/// @brief Enumeration command - responds with Lumi topology dump
void MIDIDeviceLumiKeys::enumerateLumi() {
	uint8_t command[4] = {0x01, 0x01, 0x00, 0x5D};
	sendLumiCommand(command, 4);
}

void MIDIDeviceLumiKeys::setMIDIMode(MIDIMode midiMode) {
	uint8_t command[8];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_MIDI_MODE_PREFIX;
	getCounterCodes(&command[2], (uint8_t)midiMode, MIDI_DEVICE_LUMI_KEYS_MIDI_MODE_OFFSET);

	sendLumiCommand(command, 8);
}

void MIDIDeviceLumiKeys::setMPEZone(MPEZone mpeZone) {
	uint8_t command[8];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_MPE_ZONE_PREFIX;
	getCounterCodes(&command[2], (uint8_t)mpeZone, MIDI_DEVICE_LUMI_KEYS_MPE_ZONE_OFFSET);

	sendLumiCommand(command, 8);
}

void MIDIDeviceLumiKeys::setMPENumChannels(uint8_t numChannels) {
	uint8_t command[8];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_MPE_CHANNELS_PREFIX;
	getCounterCodes(&command[2], numChannels - 1, MIDI_DEVICE_LUMI_KEYS_MPE_CHANNELS_OFFSET);

	sendLumiCommand(command, 8);
}

void MIDIDeviceLumiKeys::setRootNote(RootNote rootNote) {
	uint8_t command[8];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_ROOT_NOTE_PREFIX;
	getCounterCodes(&command[2], (uint8_t)rootNote, MIDI_DEVICE_LUMI_KEYS_ROOT_NOTE_OFFSET);

	sendLumiCommand(command, 8);
}

// Efficient binary comparison of notes to Lumi builtin scales
MIDIDeviceLumiKeys::Scale MIDIDeviceLumiKeys::determineScaleFromNotes(uint8_t* modeNotes, uint8_t noteCount) {
	uint16_t noteInt = 0;

	// Turn notes in octave into 12 bit binary
	for (uint8_t note = 0; note < noteCount; note++) {
		noteInt |= 1 << modeNotes[note];
	}

	// Compare with pre-built binary list of scales
	for (uint8_t scale = 0; scale < MIDI_DEVICE_LUMI_KEYS_SCALE_COUNT; scale++) {
		if (noteInt == scaleNotes[scale]) {
			return Scale(scale);
		}
	}

	return Scale::CHROMATIC;
}

void MIDIDeviceLumiKeys::setScale(Scale scale) {
	uint8_t command[8];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_SCALE_PREFIX;
	getCounterCodes(&command[2], (uint8_t)scale, MIDI_DEVICE_LUMI_KEYS_SCALE_OFFSET);

	sendLumiCommand(command, 8);
}

void MIDIDeviceLumiKeys::setColour(ColourZone zone, RGB rgb) {
	uint64_t colourBits = 0b00100 | rgb.b << 6 | rgb.g << 15 | rgb.r << 24 | (uint64_t)0b11111100 << 32;

	uint8_t command[7];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = zone == ColourZone::ROOT ? MIDI_DEVICE_LUMI_KEYS_CONFIG_ROOT_COLOUR_PREFIX
	                                      : MIDI_DEVICE_LUMI_KEYS_CONFIG_GLOBAL_COLOUR_PREFIX;
	command[2] = (uint8_t)colourBits;
	command[3] = (uint8_t)(colourBits >> 8);
	command[4] = (uint8_t)(colourBits >> 16);
	command[5] = (uint8_t)(colourBits >> 24);
	command[6] = (uint8_t)(colourBits >> 32);

	sendLumiCommand(command, 7);
};
