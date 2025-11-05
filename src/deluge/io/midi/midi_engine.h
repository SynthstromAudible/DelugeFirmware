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

class MIDICable;
class MIDIDrum;
class MIDIInstrument;
class MidiFollow;
class PlaybackHandler;

/// The source of a MIDI event. Can be one of a few different things, though we only keep track of the memory address of
/// the source and use that to distinguish between separate sources.
struct MIDISource {
	void const* source_{nullptr};

	MIDISource() = default;
	~MIDISource() = default;

	MIDISource(MIDICable const* cable) : source_(cable) {};
	MIDISource(MIDIInstrument const* instrument) : source_(instrument) {};
	MIDISource(PlaybackHandler const* handler) : source_(handler) {};
	MIDISource(MidiFollow const* follow) : source_(follow) {};
	MIDISource(MIDIDrum const* drum) : source_(drum) {};
	MIDISource(Sound const* sound) : source_(sound) {};

	MIDISource(MIDICable const& cable) : source_(&cable) {};
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
	/// Send a MIDI note to specific output device(s)
	/// @param deviceFilter Device selection: 0=ALL devices, 1=DIN only, 2+=USB device (index = deviceFilter-2)
	void sendNote(MIDISource source, bool on, int32_t note, uint8_t velocity, uint8_t channel, int32_t filter,
	              uint8_t deviceFilter);
	void sendCC(MIDISource source, int32_t channel, int32_t cc, int32_t value, int32_t filter);

	void sendMidi(MIDISource source, MIDIMessage message, int32_t filter = kMIDIOutputFilterNoMPE, bool sendUSB = true);
	/// Send a MIDI message to specific output device(s)
	/// @param deviceFilter Device selection: 0=ALL devices, 1=DIN only, 2+=USB device (index = deviceFilter-2)
	void sendMidi(MIDISource source, MIDIMessage message, int32_t filter, bool sendUSB, uint8_t deviceFilter);
	void sendClock(MIDISource source, bool sendUSB = true, int32_t howMany = 1);
	void sendStart(MIDISource source);
	void sendStop(MIDISource source);
	void sendPositionPointer(MIDISource source, uint16_t positionPointer);
	void sendContinue(MIDISource source);

	void checkIncomingMidi();
	void flushMIDI();
	void sendUsbMidi(MIDIMessage message, int32_t filter);
	/// Send USB MIDI message to specific device(s)
	/// @param deviceFilter Device selection: 0=ALL devices, 1=DIN only (no USB), 2+=USB device (index = deviceFilter-2)
	void sendUsbMidi(MIDIMessage message, int32_t filter, uint8_t deviceFilter);

	void sendPGMChange(MIDISource source, int32_t channel, int32_t pgm, int32_t filter);
	void sendAllNotesOff(MIDISource source, int32_t channel, int32_t filter);
	void sendBank(MIDISource source, int32_t channel, int32_t num, int32_t filter);
	void sendSubBank(MIDISource source, int32_t channel, int32_t num, int32_t filter);
	/// Send pitch bend
	///
	/// @param bend Bend amount. Only the lower 14 bits are used
	void sendPitchBend(MIDISource source, int32_t channel, uint16_t bend, int32_t filter);
	void sendChannelAftertouch(MIDISource source, int32_t channel, uint8_t value, int32_t filter);
	void sendPolyphonicAftertouch(MIDISource source, int32_t channel, uint8_t value, uint8_t noteCode, int32_t filter);
	bool anythingInOutputBuffer();

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

	/// Inject a MIDI message for processing into the event stream
	///
	/// @param cable Source cable for MIDI message
	/// @param statusType MIDI status byte
	/// @param channel source MIDI channel
	/// @param data1 Optional data byte. Validity depends on statusType
	/// @param data2 Optional data byte. Validity depends on statusType
	/// @param timer Timestamp corresponding to this byte reception. Null if the source device doesn't provide timing
	/// data.
	void midiMessageReceived(MIDICable& cable, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2,
	                         uint32_t* timer = nullptr);

	/// Notify reception of a MIDI sysex block.
	///
	/// @param cable Source cable for the sysex data
	/// @param data sysex data block, not including the start and stop bytes
	/// @param len number of bytes in data
	void midiSysexReceived(MIDICable& cable, uint8_t* data, int32_t len);

private:
	uint8_t serialMidiInput[3];
	uint8_t numSerialMidiInput;

	bool currentlyReceivingSysExSerial;

	using EventStackStorage = std::array<MIDISource, 16>;
	/// Storage for the stack of currently being processed MIDI events. When a new event is received, this is searched
	/// to make sure it wasn't generated in a loop.
	EventStackStorage eventStack_;
	/// Top of the event stack. If this is equal to eventStack_.begin(), the stack is empty.
	EventStackStorage::iterator eventStackTop_;
};

extern MidiEngine midiEngine;

extern "C" {
#endif
#ifdef __cplusplus
}
#endif
