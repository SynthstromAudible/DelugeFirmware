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

#pragma once
#include "libdeluge/midi_io.h" // DelugeMidiPort
#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "util/containers.h"
class Serializer;
class Deserializer;
class MIDICableUSBHosted;

#else
#include "definitions.h"
struct MIDICableUSB;
#endif

#ifdef __cplusplus
/* A ConnectedUSBMIDIDevice is the app-side, purely logical record of one USB-MIDI
 * device slot: which cables hang off it, whether we may send to it, and the
 * <libdeluge/midi_io.h> port that carries its byte stream. The USB transport
 * itself (buffers, transfer descriptors, send/receive state machine, connection
 * tracking) lives below the boundary in the BSP; reads/writes here move whole
 * 4-byte USB-MIDI event packets through deluge_midi_read_timed()/_write() on the
 * device's port, packed/decoded with the deluge_midi protocol library.
 */
class ConnectedUSBMIDIDevice {
public:
	MIDICableUSB* cable[4]{}; // If NULL, then no cable is connected here
	ConnectedUSBMIDIDevice();
	// Queue a pre-packed USB-MIDI event word for this device (one 4-byte event
	// packet written to its boundary port).
	void bufferMessage(uint32_t fullMessage);
	void setup();

	// Free space in the device's send buffer, in bytes of serial MIDI.
	int sendBufferSpace();
#else
// warning - accessed as a C struct from usb driver
struct ConnectedUSBMIDIDevice {
	struct MIDICableUSB* device[4];
#endif
	uint8_t maxPortConnected;

	// App policy: whether we send MIDI to this device at all (cleared for devices
	// that must not receive any, e.g. LUMI Keys). Set on (re)connection from the
	// device's name; read by the send paths.
	uint8_t canHaveMIDISent;

	// The <libdeluge/midi_io.h> port carrying this device slot's USB-MIDI byte
	// stream. Assigned once by MIDIDeviceManager::init().
	DelugeMidiPort port;
};

#ifdef __cplusplus
namespace MIDIDeviceManager {

/// Assign each ConnectedUSBMIDIDevice its boundary MIDI port and initialise the
/// MIDI transport. Must run once at startup before the USB stack is opened.
void init();

void slowRoutine();
MIDICable* readDeviceReferenceFromFile(Deserializer& reader);
void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile(Deserializer& reader);
void readDevicesFromFile();

extern MIDICableUSBUpstream upstreamUSBMIDICable1;
extern MIDICableUSBUpstream upstreamUSBMIDICable2;
extern MIDICableUSBUpstream upstreamUSBMIDICable3;
extern MIDICableDINPorts dinMIDIPorts;

extern bool differentiatingInputsByDevice;

/// Hosted USB MIDI cables, sorted case-insensitively by name (may contain entries with empty names, which sort first)
extern deluge::fast_vector<MIDICableUSBHosted*> hostedMIDIDevices;

extern uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
extern uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput;
extern bool anyChangesToSave;
} // namespace MIDIDeviceManager

#endif

extern struct ConnectedUSBMIDIDevice connectedUSBMIDIDevices[][MAX_NUM_USB_MIDI_DEVICES];
