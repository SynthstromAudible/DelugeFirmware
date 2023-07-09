/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "playback/playback_handler.h"
#include "definitions.h"
#include "io/midi/learned_midi.h"

class MIDIDevice;

class MidiEngine {
public:
	MidiEngine();

	void sendNote(bool on, int note, uint8_t velocity, uint8_t channel, int filter);
	void sendCC(int channel, int cc, int value, int filter);
	bool checkIncomingSerialMidi();
	void checkIncomingUsbMidi();

	void checkIncomingUsbSysex(uint8_t const* message, int ip, int d, int cable);

	void sendMidi(uint8_t statusType, uint8_t channel, uint8_t data1 = 0, uint8_t data2 = 0,
	              int filter = MIDI_OUTPUT_FILTER_NO_MPE, bool sendUSB = true);
	void sendClock(bool sendUSB = true, int howMany = 1);
	void sendStart();
	void sendStop();
	void sendPositionPointer(uint16_t positionPointer);
	void sendContinue();
	void flushMIDI();
	void sendUsbMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2, int filter);

	void sendSerialMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);
	void sendPGMChange(int channel, int pgm, int filter);
	void sendAllNotesOff(int channel, int filter);
	void sendBank(int channel, int num, int filter);
	void sendSubBank(int channel, int num, int filter);
	void sendPitchBend(int channel, uint8_t lsbs, uint8_t msbs, int filter);
	void sendChannelAftertouch(int channel, uint8_t value, int filter);
	void sendPolyphonicAftertouch(int channel, uint8_t value, uint8_t noteCode, int filter);
	bool anythingInOutputBuffer();
	void setupUSBHostReceiveTransfer(int ip, int midiDeviceNum);
	void flushUSBMIDIOutput();

	LearnedMIDI globalMIDICommands
	    [NUM_GLOBAL_MIDI_COMMANDS]; // If bit "16" (actually bit 4) is 1, this is a program change. (Wait, still?)

	bool midiThru;

private:
	uint8_t serialMidiInput[3];
	uint8_t numSerialMidiInput;
	uint8_t lastStatusByteSent;

	bool currentlyReceivingSysExSerial;

	int getMidiMessageLength(uint8_t statusuint8_t);
	void midiMessageReceived(MIDIDevice* fromDevice, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2,
	                         uint32_t* timer = NULL);

	// or midi device?? whatever makes sense for a consumer as a reply address.
	void midiSysexReceived(MIDIDevice* device, uint8_t* data, int len);
	int getPotentialNumConnectedUSBMIDIDevices(int ip);
};

uint32_t setupUSBMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);

extern MidiEngine midiEngine;
extern bool anythingInUSBOutputBuffer;

extern "C" {
#endif
extern uint16_t g_usb_usbmode;

// used in host mode
void usbSendComplete(int ip);
// used in peripheral mode
void usbSendCompleteAsPeripheral(int ip);
#ifdef __cplusplus
}
#endif
