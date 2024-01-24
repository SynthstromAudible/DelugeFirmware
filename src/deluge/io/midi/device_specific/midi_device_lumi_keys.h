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

#pragma once

#include "gui/colour/colour.h"
#include "io/midi/midi_device.h"

#define MIDI_DEVICE_LUMI_KEYS_VP_COUNT 1

#define MIDI_DEVICE_LUMI_KEYS_SYSEX_START 0xF0
#define MIDI_DEVICE_LUMI_KEYS_SYSEX_END 0xF7
#define MIDI_DEVICE_LUMI_KEYS_SYSEX_SPACER 0x77
#define MIDI_DEVICE_LUMI_KEYS_DEVICE 0x00 // All Devices

// Config codes, counts, and counter offsets
#define MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX 0x10

#define MIDI_DEVICE_LUMI_KEYS_MPE_ZONE_PREFIX 0x00
#define MIDI_DEVICE_LUMI_KEYS_MPE_ZONE_OFFSET 5
#define MIDI_DEVICE_LUMI_KEYS_MPE_ZONE_COUNT 2

#define MIDI_DEVICE_LUMI_KEYS_MPE_CHANNELS_PREFIX 0x10
#define MIDI_DEVICE_LUMI_KEYS_MPE_CHANNELS_OFFSET 1
#define MIDI_DEVICE_LUMI_KEYS_MPE_CHANNELS_COUNT 15

#define MIDI_DEVICE_LUMI_KEYS_MIDI_MODE_PREFIX 0x20
#define MIDI_DEVICE_LUMI_KEYS_MIDI_MODE_OFFSET 0
#define MIDI_DEVICE_LUMI_KEYS_MIDI_MODE_COUNT 3

#define MIDI_DEVICE_LUMI_KEYS_BEND_RANGE_PREFIX 0x30
#define MIDI_DEVICE_LUMI_KEYS_BEND_RANGE_OFFSET 0
#define MIDI_DEVICE_LUMI_KEYS_BEND_RANGE_COUNT 97

#define MIDI_DEVICE_LUMI_KEYS_ROOT_NOTE_PREFIX 0x30
#define MIDI_DEVICE_LUMI_KEYS_ROOT_NOTE_OFFSET 3
#define MIDI_DEVICE_LUMI_KEYS_ROOT_NOTE_COUNT 12

#define MIDI_DEVICE_LUMI_KEYS_SCALE_PREFIX 0x60
#define MIDI_DEVICE_LUMI_KEYS_SCALE_OFFSET 2
#define MIDI_DEVICE_LUMI_KEYS_SCALE_COUNT 19

#define MIDI_DEVICE_LUMI_KEYS_CONFIG_ROOT_COLOUR_PREFIX 0x30
#define MIDI_DEVICE_LUMI_KEYS_CONFIG_GLOBAL_COLOUR_PREFIX 0x20

#define MIDI_DEVICE_LUMI_SCALE_MAJOR 0b101010110101
#define MIDI_DEVICE_LUMI_SCALE_MINOR 0b010110101101
#define MIDI_DEVICE_LUMI_SCALE_HARMONIC_MINOR 0b100110101101
#define MIDI_DEVICE_LUMI_SCALE_PENTATONIC_NEUTRAL 0b010010100101
#define MIDI_DEVICE_LUMI_SCALE_PENTATONIC_MAJOR 0b001010010101
#define MIDI_DEVICE_LUMI_SCALE_PENTATONIC_MINOR 0b010010101001
#define MIDI_DEVICE_LUMI_SCALE_BLUES 0b010011101001
#define MIDI_DEVICE_LUMI_SCALE_DORIAN 0b011010101101
#define MIDI_DEVICE_LUMI_SCALE_PHRYGIAN 0b010110101011
#define MIDI_DEVICE_LUMI_SCALE_LYDIAN 0b101011010101
#define MIDI_DEVICE_LUMI_SCALE_MIXOLYDIAN 0b011010110101
#define MIDI_DEVICE_LUMI_SCALE_LOCRIAN 0b010101101011
#define MIDI_DEVICE_LUMI_SCALE_WHOLE_TONE 0b010101010101
#define MIDI_DEVICE_LUMI_SCALE_ARABIC_A 0b101101101101
#define MIDI_DEVICE_LUMI_SCALE_ARABIC_B 0b010101110101
#define MIDI_DEVICE_LUMI_SCALE_JAPANESE 0b100011010001
#define MIDI_DEVICE_LUMI_SCALE_RYUKYU 0b100010110001
#define MIDI_DEVICE_LUMI_SCALE_8TONE_SPANISH 0b010101111011
#define MIDI_DEVICE_LUMI_SCALE_CHROMATIC 0b111111111111

class MIDIDeviceLumiKeys final : public MIDIDeviceUSBHosted {
public:
	static constexpr uint16_t lumiKeysVendorProductPairs[MIDI_DEVICE_LUMI_KEYS_VP_COUNT][2] = {{0x2af4, 0xe00}};

	static constexpr uint8_t sysexManufacturer[3] = {0x00, 0x21, 0x10};

	enum class RootNote { C = 0, C_SHARP, D, D_SHARP, E, F, F_SHARP, G, G_SHARP, A, A_SHARP, B };

	enum class MIDIMode { MULTI = 0, MPE, SINGLE };

	enum class MPEZone { LOWER = 0, UPPER };

	enum class Scale {
		MAJOR = 0, // IONIAN
		MINOR,     // AEOLIAN
		HARMONIC_MINOR,
		PENTATONIC_NEUTRAL,
		PENTATONIC_MAJOR,
		PENTATONIC_MINOR,
		BLUES,
		DORIAN,
		PHRYGIAN,
		LYDIAN,
		MIXOLYDIAN,
		LOCRIAN,
		WHOLE_TONE,
		ARABIC_A,
		ARABIC_B,
		JAPANESE,
		RYUKYU,
		EIGHT_TONE_SPANISH,
		CHROMATIC
	};

	static constexpr uint16_t scaleNotes[MIDI_DEVICE_LUMI_KEYS_SCALE_COUNT] = {
	    MIDI_DEVICE_LUMI_SCALE_MAJOR,
	    MIDI_DEVICE_LUMI_SCALE_MINOR,
	    MIDI_DEVICE_LUMI_SCALE_HARMONIC_MINOR,
	    MIDI_DEVICE_LUMI_SCALE_PENTATONIC_NEUTRAL,
	    MIDI_DEVICE_LUMI_SCALE_PENTATONIC_MAJOR,
	    MIDI_DEVICE_LUMI_SCALE_PENTATONIC_MINOR,
	    MIDI_DEVICE_LUMI_SCALE_BLUES,
	    MIDI_DEVICE_LUMI_SCALE_DORIAN,
	    MIDI_DEVICE_LUMI_SCALE_PHRYGIAN,
	    MIDI_DEVICE_LUMI_SCALE_LYDIAN,
	    MIDI_DEVICE_LUMI_SCALE_MIXOLYDIAN,
	    MIDI_DEVICE_LUMI_SCALE_LOCRIAN,
	    MIDI_DEVICE_LUMI_SCALE_WHOLE_TONE,
	    MIDI_DEVICE_LUMI_SCALE_ARABIC_A,
	    MIDI_DEVICE_LUMI_SCALE_ARABIC_B,
	    MIDI_DEVICE_LUMI_SCALE_JAPANESE,
	    MIDI_DEVICE_LUMI_SCALE_RYUKYU,
	    MIDI_DEVICE_LUMI_SCALE_8TONE_SPANISH,
	    MIDI_DEVICE_LUMI_SCALE_CHROMATIC};

	enum class ColourZone { ROOT = 0, GLOBAL };

	static bool matchesVendorProduct(uint16_t vendorId, uint16_t productId);
	void hookOnConnected() override;
	void hookOnChangeRootNote() override;
	void hookOnChangeScale() override;
	void hookOnEnterScaleMode() override;
	void hookOnExitScaleMode() override;
	void hookOnMIDILearn() override;
	void hookOnRecalculateColour() override;
	void hookOnTransitionToSessionView() override;
	void hookOnTransitionToClipView() override;
	void hookOnTransitionToArrangerView() override;
	void hookOnWriteHostedDeviceToFile() override;

private:
	uint8_t sysexChecksum(uint8_t* chkBytes, uint8_t size);
	void sendLumiCommand(uint8_t* command, uint8_t length);

	// Calculates the two sysex codes aligning with a given index, for ranges
	void getCounterCodes(uint8_t* destination, int32_t index, uint8_t valueOffset = 0);

	void enumerateLumi();

	void setMIDIMode(MIDIMode midiMode);
	void setMPEZone(MPEZone mpeZone);
	void setMPENumChannels(uint8_t numChannels);
	void setRootNote(RootNote rootNote);
	Scale determineScaleFromNotes(uint8_t* modeNotes, uint8_t noteCount);
	void setScale(Scale scale);
	void setColour(ColourZone zone, RGB rgb);
};
