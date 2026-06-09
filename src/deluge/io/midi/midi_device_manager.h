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
#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "util/container/vector/named_thing_vector.h"
class Serializer;
class Deserializer;

#else
#include "definitions.h"
struct MIDICableUSB;
#endif

// The USB-MIDI transport state (RX/TX buffers, send ring, completion counters and
// the send-buffer sizing) lives in the BSP usb_midi module as `BspUsbMidiDevice`;
// see docs/dev/usb_midi_transport_relocation.md. ConnectedUSBMIDIDevice keeps only
// the logical cable fields and points at its transport block.
struct BspUsbMidiDevice;

#ifdef __cplusplus
/*A ConnectedUSBMIDIDevice is used directly to interface with the USB driver
 * When a ConnectedUSBMIDIDevice has a numMessagesQueued>=MIDI_SEND_BUFFER_LEN and tries to add another,
 * all outputs are sent. The send routine calls the USB output function, points the
 * USB pipes FIFO buffer directly at the dataSendingNow array, and then sends.
 * Sends can also be triggered by the midiAndGateOutput interrupt
 *
 * Reads are more complicated.
 * Actual reads are done by usb_cstd_usb_task, which has a commented out interrupt associated
 * The function is instead called in the midiengine::checkincomingUSBmidi function, which is called
 * in the audio engine loop
 *
 * The USB read function is configured by setupUSBHostReceiveTransfer, which is called to
 * setup the next device after each successful read. Data is written directly into the receiveData
 * array from the USB device, it's set as the USB pipe address during midi engine setup
 */
class ConnectedUSBMIDIDevice {
public:
	MIDICableUSB* cable[4]; // If NULL, then no cable is connected here
	ConnectedUSBMIDIDevice();
	// Append a pre-packed USB-MIDI event word to this device's send ring (forwards
	// to the BSP usb_midi transport).
	void bufferMessage(uint32_t fullMessage);
	void setup();

	// Free space in the send ring, in bytes of serial MIDI (forwards to the BSP).
	int sendBufferSpace();
#else
// warning - accessed as a C struct from usb driver
struct ConnectedUSBMIDIDevice {
	struct MIDICableUSB* device[4];
#endif
	uint8_t maxPortConnected;

	// USB-MIDI transport state (RX/TX buffers, send ring, completion counters)
	// owned by the BSP usb_midi module. Wired by MIDIDeviceManager::init() at
	// startup; the transport methods below operate through it.
	struct BspUsbMidiDevice* transport;
};

#ifdef __cplusplus
namespace MIDIDeviceManager {

/// Wire each ConnectedUSBMIDIDevice to its BSP-owned transport block and zero the
/// transport state. Must run once at startup before the USB stack is opened.
void init();

void slowRoutine();
MIDICable* readDeviceReferenceFromFile(Deserializer& reader);
void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory);
void readMidiFollowDeviceReferenceFromFlash(MIDIFollowChannelType whichType, uint8_t const* memory);
void writeMidiFollowDeviceReferenceToFlash(MIDIFollowChannelType whichType, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile(Deserializer& reader);
void readDevicesFromFile();

extern MIDICableUSBUpstream upstreamUSBMIDICable1;
extern MIDICableUSBUpstream upstreamUSBMIDICable2;
extern MIDICableUSBUpstream upstreamUSBMIDICable3;
extern MIDICableDINPorts dinMIDIPorts;

extern bool differentiatingInputsByDevice;

extern NamedThingVector hostedMIDIDevices;

extern uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
extern uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput;
extern bool anyChangesToSave;
} // namespace MIDIDeviceManager

#endif

extern struct ConnectedUSBMIDIDevice connectedUSBMIDIDevices[][MAX_NUM_USB_MIDI_DEVICES];
