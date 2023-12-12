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
#define MIDI_DEVICE_LUMI_KEYS_KEY_PREFIX 0x30
#define MIDI_DEVICE_LUMI_KEYS_KEY_COUNT 12
#define MIDI_DEVICE_LUMI_KEYS_SCALE_PREFIX 0x60
#define MIDI_DEVICE_LUMI_KEYS_SCALE_COUNT 19

#define MIDI_DEVICE_LUMI_KEYS_CONFIG_ROOT_COLOR_PREFIX 0x30
#define MIDI_DEVICE_LUMI_KEYS_CONFIG_GLOBAL_COLOR_PREFIX 0x20

class MIDIDeviceLumiKeys final : public MIDIDeviceUSBHosted {
public:
	static constexpr uint16_t lumiKeysVendorProductPairs[MIDI_DEVICE_LUMI_KEYS_VP_COUNT][2] = {{0x2af4, 0xe00}};

	static constexpr uint8_t sysexManufacturer[3] = {0x00, 0x21, 0x10};

	enum class Key {
		C = 0,
		C_SHARP,
		D,
		D_SHARP,
		E,
		F,
		F_SHARP,
		G,
		G_SHARP,
		A,
		A_SHARP,
		B
	};

	static constexpr uint8_t sysexKeyCodes[MIDI_DEVICE_LUMI_KEYS_KEY_COUNT][2] = {
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

	enum class Scale {
		MAJOR = 0,
		MINOR,
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

	static constexpr uint8_t sysexScaleCodes[MIDI_DEVICE_LUMI_KEYS_SCALE_COUNT][2] = {
	    {0x02, 0x00}, {0x22, 0x00}, {0x42, 0x00}, {0x62, 0x00}, {0x02, 0x01}, {0x22, 0x01}, {0x42, 0x01},
	    {0x62, 0x01}, {0x02, 0x02}, {0x22, 0x02}, {0x42, 0x02}, {0x62, 0x02}, {0x02, 0x03}, {0x22, 0x03},
	    {0x42, 0x03}, {0x62, 0x03}, {0x02, 0x04}, {0x22, 0x04}, {0x42, 0x04}};

	static bool matchesVendorProduct(uint16_t vendorId, uint16_t productId);
	void hookOnConnected() override;
	void hookOnChangeKeyOrScale() override;

private:
	uint8_t sysexChecksum(uint8_t* chk_bytes, uint8_t size);
	void sendLumiCommand(uint8_t* command, uint8_t length);
};
