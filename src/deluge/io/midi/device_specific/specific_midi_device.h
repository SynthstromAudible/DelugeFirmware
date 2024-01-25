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

#include "io/midi/device_specific/midi_device_lumi_keys.h"
#include "io/midi/midi_device.h"

enum class SpecificMidiDeviceType { NONE = 0, LUMI_KEYS = 1 };

SpecificMidiDeviceType getSpecificMidiDeviceType(uint16_t vendorId, uint16_t productId);

MIDIDeviceUSBHosted* recastSpecificMidiDevice(void* sourceDevice);
MIDIDeviceUSBHosted* recastSpecificMidiDevice(MIDIDeviceUSBHosted* sourceDevice);

MIDIDeviceUSBHosted* getSpecificDeviceFromMIDIDevice(MIDIDevice* sourceDevice);

void iterateAndCallSpecificDeviceHook(MIDIDeviceUSBHosted::Hook hook);
