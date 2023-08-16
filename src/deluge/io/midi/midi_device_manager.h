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
#include "util/container/vector/named_thing_vector.h"
class MIDIDevice;
class MIDIDeviceUSBUpstream;
class MIDIDeviceDINPorts;
class MIDIDeviceUSB;

#else
#include "definitions.h"
struct MIDIDeviceUSB;
#endif

// size in 32-bit messages
// NOTE: increasing this even more doesn't work.
// Looks like a hardware limitation (maybe we more in FS mode)?
#define MIDI_SEND_BUFFER_LEN_INNER 32
#define MIDI_SEND_BUFFER_LEN_INNER_HOST 16

// MUST be an exact power of two
#define MIDI_SEND_BUFFER_LEN_RING 1024
#define MIDI_SEND_RING_MASK (MIDI_SEND_BUFFER_LEN_RING - 1)

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
	MIDIDeviceUSB* device[4]; // If NULL, then no device is connected here
	void bufferMessage(uint32_t fullMessage);
	void setup();

	// move data from ring buffer to dataSendingNow, assuming it is free
	bool consumeSendData();
	bool hasBufferedSendData();
	int sendBufferSpace();
#else
//warning - accessed as a C struct from usb driver
struct ConnectedUSBMIDIDevice {
	struct MIDIDeviceUSB* device[4];
#endif
	uint8_t currentlyWaitingToReceive;
	uint8_t sq; // Only for connections as HOST
	uint8_t canHaveMIDISent;
	uint16_t numBytesReceived;
	uint8_t receiveData[64];

	// This buffer is passed directly to the USB driver, and is limited to what the hardware allows
	uint8_t dataSendingNow[MIDI_SEND_BUFFER_LEN_INNER * 4];
	// This will show a value after the general flush function is called, throughout other Devices being sent to before this one, and until we've completed our send
	uint8_t numBytesSendingNow;

	// This is a ring buffer for data waiting to be sent which doesn't fit the smaller buffer above.
	// Any code which wants to send midi data would use the writing side and append more messages.
	// When we are ready to send data on this device, we consume data on the reading side and move it into the
	// smaller dataSendingNow buffer above.
	uint32_t sendDataRingBuf[MIDI_SEND_BUFFER_LEN_RING];
	uint32_t ringBufWriteIdx;
	uint32_t ringBufReadIdx;

	uint8_t maxPortConnected;
};

#ifdef __cplusplus
namespace MIDIDeviceManager {

void slowRoutine();
MIDIDevice* readDeviceReferenceFromFile();
void readDeviceReferenceFromFlash(GlobalMIDICommand whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(GlobalMIDICommand whichCommand, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile();
void readDevicesFromFile();

extern MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port1;
extern MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port2;
extern MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port3;
extern MIDIDeviceDINPorts dinMIDIPorts;

extern bool differentiatingInputsByDevice;

extern NamedThingVector hostedMIDIDevices;

extern uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
extern uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput;
extern bool anyChangesToSave;
} // namespace MIDIDeviceManager

#endif

extern struct ConnectedUSBMIDIDevice connectedUSBMIDIDevices[][MAX_NUM_USB_MIDI_DEVICES];
