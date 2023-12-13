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
	sendMCMsNowIfNeeded();

	enumerateLumi();
	sendBufferSpace();

	if (currentSong != NULL) {
		setRootNote(RootNote(currentSong->rootNote % kOctaveSize));
		Scale scale = determineScaleFromNotes(currentSong->modeNotes, currentSong->numModeNotes);
		setScale(scale);
	}

	sendBufferSpace();
}

void MIDIDeviceLumiKeys::hookOnChangeRootNote(int16_t rootNote) {
	setRootNote(RootNote(rootNote % kOctaveSize));
}

void MIDIDeviceLumiKeys::hookOnChangeScale(uint8_t* scaleNotes, uint8_t noteCount) {
	Scale scale = determineScaleFromNotes(scaleNotes, noteCount);
	setScale(scale);
}

void MIDIDeviceLumiKeys::hookOnLearn() {
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

// Enumeration command - responds with Lumi topology dump
void MIDIDeviceLumiKeys::enumerateLumi() {
	uint8_t command[4] = {0x01, 0x01, 0x00, 0x5D};
	sendLumiCommand(command, 4);
}

void MIDIDeviceLumiKeys::setRootNote(RootNote rootNote) {
	uint8_t command[4];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_KEY_PREFIX;
	memcpy(&command[2], sysexRootNoteCodes[(uint8_t)rootNote], 2);

	sendLumiCommand(command, 4);
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
	uint8_t command[4];
	command[0] = MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX;
	command[1] = MIDI_DEVICE_LUMI_KEYS_SCALE_PREFIX;
	memcpy(&command[2], sysexScaleCodes[(uint8_t)scale], 2);

	sendLumiCommand(command, 4);
}
