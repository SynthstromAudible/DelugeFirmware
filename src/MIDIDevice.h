/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#ifndef MIDIDEVICE_H_
#define MIDIDEVICE_H_

#include "DString.h"
#include "definitions.h"
#include "ModelStack.h"

// These numbers are what get stored just in the internal Deluge flash memory to represent things.
#define VENDOR_ID_NONE 0
#define VENDOR_ID_UPSTREAM_USB 1
#define VENDOR_ID_DIN 2

#define MIDI_DIRECTION_INPUT_TO_DELUGE 0
#define MIDI_DIRECTION_OUTPUT_FROM_DELUGE 1

#define MPE_ZONE_LOWER_NUMBERED_FROM_0 0
#define MPE_ZONE_UPPER_NUMBERED_FROM_0 1

class MIDIDevice;

class MIDIPort {
public:
	MIDIPort() {
		mpeLowerZoneLastMemberChannel = 0;
		mpeUpperZoneLastMemberChannel = 15;
	}
	int channelToZone(int inputChannel);
	void writeToFile(char const* tagName);
	bool worthWritingToFile();
	void readFromFile(MIDIDevice* deviceToSendMCMsOn);
	void moveUpperZoneOutOfWayOfLowerZone();
	void moveLowerZoneOutOfWayOfUpperZone();

	inline bool isChannelPartOfAnMPEZone(int channel) {
		return (channel >= 1 && channel <= 14
		        && (mpeLowerZoneLastMemberChannel >= channel || mpeUpperZoneLastMemberChannel <= channel));
	}

	uint8_t mpeLowerZoneLastMemberChannel; // 0 means off
	uint8_t mpeUpperZoneLastMemberChannel; // 15 means off
};

class MIDIInputChannel {
public:
	MIDIInputChannel() {
		bendRange =
		    0; // Means not set; don't copy value. Also, note this is the "main" bend range; there isn't one for finger-level because this is a non-MPE single MIDI channel.
		rpnLSB = 127; // Means no param specified
		rpnMSB = 127;
	}
	uint8_t rpnLSB;
	uint8_t rpnMSB;
	uint8_t bendRange;
};

// These never get destructed. So we're safe having various Instruments etc holding pointers to them.
class MIDIDevice {
public:
	MIDIDevice();
	void writeReferenceToFile(char const* tagName = "device");
	virtual void writeToFlash(uint8_t* memory) = 0;
	virtual char const* getDisplayName() = 0;
	void writeToFile(char const* tagName);
	void readFromFile();
	void dataEntryMessageReceived(ModelStack* modelStack, int channel, int msb);
	bool wantsToOutputMIDIOnChannel(int channel, int filter);
	void sendAllMCMs();
	bool worthWritingToFile();
	void writePorts();

	virtual void sendMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) = 0;

	inline void sendCC(int channel, int cc, int value) { sendMessage(0x0B, channel, cc, value); }

	void sendRPN(int channel, int rpnMSB, int rpnLSB, int valueMSB);

	inline bool hasDefaultVelocityToLevelSet() { return defaultVelocityToLevel; }

	MIDIPort ports
	    [2]; // I think I used an array here so the settings menu could deal with either one easily - which doesn't seem like a very strong reason really...

	// These are stored as full-range 16-bit values (scaled up from 7 or 14-bit MIDI depending on which), and you'll want to scale this up again to 32-bit to use them.
	// X and Y may be both positive and negative, and Z may only be positive (so has been scaled up less from incoming bits).
	// These default to 0.
	// These are just for MelodicInstruments. For Drums, the values get stored in the Drum itself.
	int16_t defaultInputMPEValuesPerMIDIChannel[16][NUM_EXPRESSION_DIMENSIONS];

	uint8_t mpeZoneBendRanges[2][2]; // 0 means none set. It's [zone][whichBendRange].

	MIDIInputChannel inputChannels[16];

	int32_t defaultVelocityToLevel;

	// 0 if not connected. For USB devices, the bits signal a connection of the corresponding connectedUSBMIDIDevices[].
	// Of course there'll usually just be one bit set, unless two of the same device are connected.
	uint8_t connectionFlags;

protected:
	virtual void
	writeReferenceAttributesToFile() = 0; // These go both into MIDIDEVICES.XML and also any song/preset files where there's a reference to this Device.
	void writeDefinitionAttributesToFile(); // These only go into MIDIDEVICES.XML.
};

class MIDIDeviceUSB : public MIDIDevice {
public:
	MIDIDeviceUSB() { needsToSendMCMs = 0; }
	void sendMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);
	void connectedNow(int midiDeviceNum);
	void sendMCMsNowIfNeeded();
	uint8_t needsToSendMCMs;
};

class MIDIDeviceUSBHosted final : public MIDIDeviceUSB {
public:
	MIDIDeviceUSBHosted() {}
	void writeReferenceAttributesToFile();
	void writeToFlash(uint8_t* memory);
	char const* getDisplayName();

	uint16_t vendorId;
	uint16_t productId;

	String name;
};

class MIDIDeviceUSBUpstream final : public MIDIDeviceUSB {
public:
	MIDIDeviceUSBUpstream() {}
	void writeReferenceAttributesToFile();
	void writeToFlash(uint8_t* memory);
	char const* getDisplayName();
};

class MIDIDeviceDINPorts final : public MIDIDevice {
public:
	MIDIDeviceDINPorts() {
		connectionFlags = 1; // DIN ports are always connected
	}
	void writeReferenceAttributesToFile();
	void writeToFlash(uint8_t* memory);
	char const* getDisplayName();
	void sendMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);
};

#endif /* MIDIDEVICE_H_ */
