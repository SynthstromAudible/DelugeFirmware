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
class ConnectedUSBMIDIDevice {
public:
	MIDIDeviceUSB* device; // If NULL, then no device is connected here
	void bufferMessage(uint32_t fullMessage);
	void setup();
#else
struct ConnectedUSBMIDIDevice {
	struct MIDIDeviceUSB* device;
#endif
	uint8_t currentlyWaitingToReceive;
	uint8_t sq; // Only for connections as HOST
	uint8_t canHaveMIDISent;
	uint16_t numBytesReceived;
	uint8_t receiveData[64];
	uint32_t preSendData[16];
	uint8_t dataSendingNow[64];
	uint8_t numMessagesQueued;
	uint8_t
	    numBytesSendingNow; // This will show a value after the general flush function is called, throughout other Devices being sent to before this one, and until we've completed our send
};

#ifdef __cplusplus
namespace MIDIDeviceManager {

void init();
void slowRoutine();
MIDIDevice* readDeviceReferenceFromFile();
void readDeviceReferenceFromFlash(int whichCommand, uint8_t const* memory);
void writeDeviceReferenceToFlash(int whichCommand, uint8_t* memory);
void recountSmallestMPEZones();
void writeDevicesToFile();
void readAHostedDeviceFromFile();
void readDevicesFromFile();

extern MIDIDeviceUSBUpstream upstreamUSBMIDIDevice;
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
