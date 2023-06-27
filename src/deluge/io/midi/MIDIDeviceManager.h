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

#ifndef MIDIDEVICEMANAGER_H_
#define MIDIDEVICEMANAGER_H_
#include "definitions.h"

#ifdef __cplusplus
#include "NamedThingVector.h"
class MIDIDevice;
class MIDIDeviceUSBUpstream;
class MIDIDeviceDINPorts;
class MIDIDeviceUSB;

#else
struct MIDIDeviceUSB;
#endif

#ifdef __cplusplus
/*A ConnectedUSBMIDIDevice is used directly to interface with the USB driver
 * When a ConnectedUSBMIDIDevice has a numMessagesQueued>16 and tries to add another,
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
	uint32_t preSendData[16];
	uint8_t dataSendingNow[64];
	uint8_t numMessagesQueued;

	// This will show a value after the general flush function is called, throughout other Devices being sent to before this one, and until we've completed our send
	uint8_t numBytesSendingNow;
	uint8_t maxPortConnected;
};

#ifdef __cplusplus
namespace MIDIDeviceManager {

void slowRoutine();
MIDIDevice* readDeviceReferenceFromFile();
void readDeviceReferenceFromFlash(int whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(int whichCommand, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile();
void readDevicesFromFile();

extern MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port1;
extern MIDIDeviceUSBUpstream upstreamUSBMIDIDevice_port2;
extern MIDIDeviceDINPorts dinMIDIPorts;

extern bool differentiatingInputsByDevice;

extern NamedThingVector hostedMIDIDevices;

extern uint8_t lowestLastMemberChannelOfLowerZoneOnConnectedOutput;
extern uint8_t highestLastMemberChannelOfUpperZoneOnConnectedOutput;
extern bool anyChangesToSave;
} // namespace MIDIDeviceManager

#endif

extern struct ConnectedUSBMIDIDevice connectedUSBMIDIDevices[][MAX_NUM_USB_MIDI_DEVICES];

#endif /* MIDIDEVICEMANAGER_H_ */
