/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#ifndef ARPEGGIATOR_H_
#define ARPEGGIATOR_H_

#include "definitions.h"
#include "OrderedResizeableArray.h"

class PostArpTriggerable;
class ParamManagerForTimeline;

class ArpeggiatorSettings {
public:
	ArpeggiatorSettings();

	void cloneFrom(ArpeggiatorSettings* other) {
		numOctaves = other->numOctaves;
		syncLevel = other->syncLevel;
		mode = other->mode;
	}

	uint32_t getPhaseIncrement(int32_t arpRate);

	// Settings
	uint8_t numOctaves;
	SyncLevel syncLevel;
	SyncType syncType;
	uint8_t mode;
};

struct ArpNote {
	int16_t inputCharacteristics
	    [2]; // Before arpeggiation. And applying to MIDI input if that's happening. Or, channel might be MIDI_CHANNEL_NONE.
	int16_t mpeValues[NUM_EXPRESSION_DIMENSIONS];
	uint8_t velocity;
	uint8_t outputMemberChannel;
};

#define ARP_NOTE_NONE 32767

class ArpReturnInstruction {
public:
	ArpReturnInstruction()
	    : noteCodeOffPostArp(ARP_NOTE_NONE), noteCodeOnPostArp(ARP_NOTE_NONE), sampleSyncLengthOn(0) {}
	int16_t noteCodeOffPostArp; // 32767 means none/no action
	int16_t noteCodeOnPostArp;  // 32767 means none/no action

	// These are only valid if doing a note-on
	uint32_t
	    sampleSyncLengthOn; // This defaults to zero, or may be overwritten by the caller to the Arp - and then the Arp itself may override that.
	ArpNote* arpNoteOn;

	// And these are only valid if doing a note-off
	uint8_t outputMIDIChannelOff; // For MPE
};

class ArpeggiatorBase {
public:
	virtual void noteOn(ArpeggiatorSettings* settings, int noteCode, int velocity, ArpReturnInstruction* instruction,
	                    int fromMIDIChannel, int16_t const* mpeValues) = 0;
	void render(ArpeggiatorSettings* settings, int numSamples, uint32_t gateThreshold, uint32_t phaseIncrement,
	            ArpReturnInstruction* instruction);
	int32_t doTickForward(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, uint32_t ClipCurrentPos,
	                      bool currentlyPlayingReversed);
	virtual bool hasAnyInputNotesActive() = 0;
	virtual void reset() = 0;

	bool gateCurrentlyActive;
	uint32_t gatePos;
	int8_t currentOctave;
	int8_t currentDirection;
	bool playedFirstArpeggiatedNoteYet;
	uint8_t lastVelocity;
	int16_t noteCodeCurrentlyOnPostArp;
	uint8_t outputMIDIChannelForNoteCurrentlyOnPostArp;

protected:
	virtual void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) = 0;
	void switchAnyNoteOff(ArpReturnInstruction* instruction);
};

class ArpeggiatorForDrum final : public ArpeggiatorBase {
public:
	ArpeggiatorForDrum();
	void noteOn(ArpeggiatorSettings* settings, int noteCode, int velocity, ArpReturnInstruction* instruction,
	            int fromMIDIChannel, int16_t const* mpeValues);
	void noteOff(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction);
	void reset();
	ArpNote arpNote; // For the one note. noteCode will always be 60. velocity will be 0 if off.

protected:
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction);
	bool hasAnyInputNotesActive();
};

class Arpeggiator final : public ArpeggiatorBase {
public:
	Arpeggiator();

	void reset();

	void noteOn(ArpeggiatorSettings* settings, int noteCode, int velocity, ArpReturnInstruction* instruction,
	            int fromMIDIChannel, int16_t const* mpeValues);
	void noteOff(ArpeggiatorSettings* settings, int noteCodePreArp, ArpReturnInstruction* instruction);
	bool hasAnyInputNotesActive();

	OrderedResizeableArray notes;
	int16_t whichNoteCurrentlyOnPostArp; // As in, the index within our list

protected:
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction);
};

#endif /* ARPEGGIATOR_H_ */
