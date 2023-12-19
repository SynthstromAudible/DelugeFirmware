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

#include "io/midi/midi_device.h"

#define MIDI_DEVICE_LUMI_KEYS_VP_COUNT 1

#define MIDI_DEVICE_LUMI_KEYS_SYSEX_START 0xF0
#define MIDI_DEVICE_LUMI_KEYS_SYSEX_END 0xF7
#define MIDI_DEVICE_LUMI_KEYS_SYSEX_SPACER 0x77
#define MIDI_DEVICE_LUMI_KEYS_DEVICE 0x00 // All Devices

#define MIDI_DEVICE_LUMI_KEYS_CONFIG_PREFIX 0x10
#define MIDI_DEVICE_LUMI_KEYS_MPE_ZONE_PREFIX 0x00
#define MIDI_DEVICE_LUMI_KEYS_MPE_CHANNELS_PREFIX 0x10
#define MIDI_DEVICE_LUMI_KEYS_MIDI_MODE_PREFIX 0x20
#define MIDI_DEVICE_LUMI_KEYS_KEY_PREFIX 0x30
#define MIDI_DEVICE_LUMI_KEYS_KEY_COUNT 12
#define MIDI_DEVICE_LUMI_KEYS_SCALE_PREFIX 0x60
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

	static constexpr uint8_t sysexMidiChannel[16][2] = {
	    {0x20, 0x00}, // Ch. 1
	    {0x40, 0x00}, {0x60, 0x00}, {0x00, 0x01}, {0x20, 0x01}, {0x40, 0x01}, {0x60, 0x01}, {0x00, 0x02}, {0x20, 0x02},
	    {0x40, 0x02}, {0x60, 0x02}, {0x00, 0x03}, {0x20, 0x03}, {0x40, 0x03}, {0x60, 0x03}, {0x00, 0x04} // Ch. 16
	};

	enum class RootNote { C = 0, C_SHARP, D, D_SHARP, E, F, F_SHARP, G, G_SHARP, A, A_SHARP, B };

	static constexpr uint8_t sysexRootNoteCodes[MIDI_DEVICE_LUMI_KEYS_KEY_COUNT][2] = {
	    {0x03, 0x00}, // C
	    {0x23, 0x00}, // C#
	    {0x43, 0x00}, // D
	    {0x63, 0x00}, // D#
	    {0x03, 0x01}, // E
	    {0x23, 0x01}, // F
	    {0x43, 0x01}, // F#
	    {0x63, 0x01}, // G
	    {0x03, 0x02}, // G#
	    {0x23, 0x02}, // A
	    {0x43, 0x02}, // A#
	    {0x63, 0x02}  // B
	};

	enum class MIDIMode { MPE = 0, MULTI, SINGLE };

	static constexpr uint8_t sysexMidiModeCodes[3] = {
	    0x20, // MPE
	    0x00, // Multi
	    0x40  // Single
	};

	enum class MPEZone { LOWER = 0, UPPER };

	static constexpr uint8_t SysexMpeZoneCodes[2] = {
	    0x05, // LOWER
	    0x25  // UPPER
	};

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

	// By reference to presetScaleNames in global scope
	static constexpr Scale delugeScaleMap[9] = {
	    Scale::MAJOR,      // MAJOR (IONIAN) -> MAJOR
	    Scale::MINOR,      // MINOR (AEOLIAN) -> MINOR
	    Scale::DORIAN,     // DORIAN -> DORIAN
	    Scale::PHRYGIAN,   // PHRYGIAN -> PHRYGIAN
	    Scale::LYDIAN,     // LYDIAN -> LYDIAN
	    Scale::MIXOLYDIAN, // MIXOLYDIAN -> MIXOLYDIAN,
	    Scale::LOCRIAN,    // LOCRIAN -> LOCRIAN
	    Scale::CHROMATIC,  // RANDOM -> CHROMATIC (no idea how else to handle)
	    Scale::CHROMATIC   // NONE -> CHROMATIC
	};

	static constexpr uint8_t sysexScaleCodes[MIDI_DEVICE_LUMI_KEYS_SCALE_COUNT][2] = {
	    {0x02, 0x00}, {0x22, 0x00}, {0x42, 0x00}, {0x62, 0x00}, {0x02, 0x01}, {0x22, 0x01}, {0x42, 0x01},
	    {0x62, 0x01}, {0x02, 0x02}, {0x22, 0x02}, {0x42, 0x02}, {0x62, 0x02}, {0x02, 0x03}, {0x22, 0x03},
	    {0x42, 0x03}, {0x62, 0x03}, {0x02, 0x04}, {0x22, 0x04}, {0x42, 0x04}};

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

	void enumerateLumi();

	void setMIDIMode(MIDIMode midiMode);
	void setMPEZone(MPEZone mpeZone);
	void setMPENumChannels(uint8_t numChannels);
	void setRootNote(RootNote rootNote);
	Scale determineScaleFromNotes(uint8_t* modeNotes, uint8_t noteCount);
	void setScale(Scale scale);
	void setColour(ColourZone zone, uint8_t r, uint8_t g, uint8_t b);
};
