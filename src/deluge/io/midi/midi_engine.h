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

#include "OSLikeStuff/scheduler_api.h"
#include "definitions_cxx.hpp"
#include "io/midi/learned_midi.h"
#include "playback/playback_handler.h"

class MIDIDevice;
class MIDIInstrument;
class MidiFollow;
class PlaybackHandler;
class MIDIDrum;
class Sound;

/// The source of a MIDI event. Can be one of a few different things, though we only keep track of the memory address of
/// the source and use that to distinguish between separate sources.
struct MIDISource {
	void const* source_{nullptr};

	MIDISource() = default;
	~MIDISource() = default;

	MIDISource(MIDIDevice const* device) : source_(device) {};
	MIDISource(MIDIInstrument const* instrument) : source_(instrument) {};
	MIDISource(PlaybackHandler const* handler) : source_(handler) {};
	MIDISource(MidiFollow const* follow) : source_(follow) {};
	MIDISource(MIDIDrum const* drum) : source_(drum) {};
	MIDISource(Sound const* sound) : source_(sound) {};

	MIDISource(MIDIDevice const& device) : source_(&device) {};
	MIDISource(MIDIInstrument const& instrument) : source_(&instrument) {};
	MIDISource(MIDIDrum const& drum) : source_(&drum) {};
	MIDISource(Sound const& sound) : source_(&sound) {};
	MIDISource(MidiFollow const& follow) : source_(&follow) {};
	MIDISource(PlaybackHandler const& handler) : source_(&handler) {};

	MIDISource(MIDISource const& other) = default;
	MIDISource(MIDISource&& other) = default;

	MIDISource& operator=(MIDISource const& other) = default;
	MIDISource& operator=(MIDISource&& other) = default;

	bool operator==(MIDISource const& other) const { return source_ == other.source_; }
};

class MidiEngine {
public:
	MidiEngine();

	void sendNote(MIDISource source, bool on, int32_t note, uint8_t velocity, uint8_t channel, int32_t filter);
	void sendCC(MIDISource source, int32_t channel, int32_t cc, int32_t value, int32_t filter);
	bool checkIncomingSerialMidi();
	void checkIncomingUsbMidi();

	void checkIncomingUsbSysex(uint8_t const* message, int32_t ip, int32_t d, int32_t cable);

	void sendMidi(MIDISource source, uint8_t statusType, uint8_t channel, uint8_t data1 = 0, uint8_t data2 = 0,
	              int32_t filter = kMIDIOutputFilterNoMPE, bool sendUSB = true);
	void sendClock(MIDISource source, bool sendUSB = true, int32_t howMany = 1);
	void sendStart(MIDISource source);
	void sendStop(MIDISource source);
	void sendPositionPointer(MIDISource source, uint16_t positionPointer);
	void sendContinue(MIDISource source);

	void flushMIDI();
	void sendUsbMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2, int32_t filter);
	void sendSerialMidi(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);

	void sendPGMChange(MIDISource source, int32_t channel, int32_t pgm, int32_t filter);
	void sendAllNotesOff(MIDISource source, int32_t channel, int32_t filter);
	void sendBank(MIDISource source, int32_t channel, int32_t num, int32_t filter);
	void sendSubBank(MIDISource source, int32_t channel, int32_t num, int32_t filter);
	void sendPitchBend(MIDISource source, int32_t channel, uint8_t lsbs, uint8_t msbs, int32_t filter);
	void sendChannelAftertouch(MIDISource source, int32_t channel, uint8_t value, int32_t filter);
	void sendPolyphonicAftertouch(MIDISource source, int32_t channel, uint8_t value, uint8_t noteCode, int32_t filter);
	bool anythingInOutputBuffer();
	void setupUSBHostReceiveTransfer(int32_t ip, int32_t midiDeviceNum);
	void flushUSBMIDIOutput();

	// If bit "16" (actually bit 4) is 1, this is a program change. (Wait, still?)
	LearnedMIDI globalMIDICommands[kNumGlobalMIDICommands];

	bool midiThru;
	LearnedMIDI midiFollowChannelType[kNumMIDIFollowChannelTypes]; // A, B, C
	MIDIFollowChannelType midiFollowFeedbackChannelType;           // A, B, C, NONE
	uint8_t midiFollowKitRootNote;
	bool midiFollowDisplayParam;
	MIDIFollowFeedbackAutomationMode midiFollowFeedbackAutomation;
	bool midiFollowFeedbackFilter;
	MIDITakeoverMode midiTakeover;
	bool midiSelectKitRow;
	TaskID routine_task_id;

	// shared buffer for formatting sysex messages.
	// Not safe for use in interrupts.
	uint8_t sysex_fmt_buffer[1024];

private:
	uint8_t serialMidiInput[3];
	uint8_t numSerialMidiInput;
	uint8_t lastStatusByteSent;

	bool currentlyReceivingSysExSerial;

	using EventStackStorage = std::array<MIDISource, 16>;
	/// Storage for the stack of currently being processed MIDI events. When a new event is received, this is searched
	/// to make sure it wasn't generated in a loop.
	EventStackStorage eventStack_;
	/// Top of the event stack. If this is equal to eventStack_.begin(), the stack is empty.
	EventStackStorage::iterator eventStackTop_;

	int32_t getMidiMessageLength(uint8_t statusuint8_t);
	void midiMessageReceived(MIDIDevice* fromDevice, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2,
	                         uint32_t* timer = NULL);

	void midiSysexReceived(MIDIDevice* device, uint8_t* data, int32_t len);
	int32_t getPotentialNumConnectedUSBMIDIDevices(int32_t ip);
};

uint32_t setupUSBMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);

extern MidiEngine midiEngine;
extern bool anythingInUSBOutputBuffer;

extern "C" {
#endif
extern uint16_t g_usb_usbmode;

void usbSendCompleteAsHost(int32_t ip);       // used when deluge is in host mode
void usbSendCompleteAsPeripheral(int32_t ip); // used in peripheral mode
#ifdef __cplusplus
}
#endif
