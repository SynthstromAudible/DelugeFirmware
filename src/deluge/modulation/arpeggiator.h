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

#pragma once

#include "definitions_cxx.hpp"
#include "util/container/array/ordered_resizeable_array.h"
#include "util/container/array/resizeable_array.h"

#include <array>

class PostArpTriggerable;
class ParamManagerForTimeline;

typedef struct {
	uint8_t length; // the number of steps to use, between 1 and 4
	bool steps[4];  // the steps, whether they should play a note or a silence
} ArpRhythm;

#define NUM_PRESET_ARP_RHYTHMS 8
const ArpRhythm arpRhythmPatterns[NUM_PRESET_ARP_RHYTHMS] = {
    {1, {1, 1, 1, 1}}, // <-
    {3, {1, 1, 0, 1}}, // <-
    {3, {1, 0, 1, 1}}, // <-
    {4, {1, 1, 1, 0}}, // <-
    {4, {1, 1, 0, 1}}, // <-
    {4, {1, 0, 1, 1}}, // <-
    {4, {1, 1, 0, 0}}, // <-
    {4, {1, 0, 0, 1}}, // <-
};
const std::array<char const*, NUM_PRESET_ARP_RHYTHMS> arpRhythmPatternNames = {
    "0",    // <-
    "00-",  // <-
    "0-0",  // <-
    "000-", // <-
    "00-0", // <-
    "0-00", // <-
    "00--", // <-
    "0--0", // <-
};

class ArpeggiatorSettings {
public:
	ArpeggiatorSettings();

	void cloneFrom(ArpeggiatorSettings* other) {
		preset = other->preset;
		mode = other->mode;
		octaveMode = other->octaveMode;
		noteMode = other->noteMode;
		numOctaves = other->numOctaves;
		syncType = other->syncType;
		syncLevel = other->syncLevel;
		rhythm = other->rhythm;
		mpeVelocity = other->mpeVelocity;
		mpeGate = other->mpeGate;
		mpeRatchetAmount = other->mpeRatchetAmount;
		mpeRatchetProbability = other->mpeRatchetProbability;
		mpeOctaves = other->mpeOctaves;
	}

	void updatePresetFromCurrentSettings() {
		if (mode == ArpMode::OFF) {
			preset = ArpPreset::OFF;
		}
		else if (octaveMode == ArpOctaveMode::UP && noteMode == ArpNoteMode::UP) {
			preset = ArpPreset::UP;
		}
		else if (octaveMode == ArpOctaveMode::DOWN && noteMode == ArpNoteMode::DOWN) {
			preset = ArpPreset::DOWN;
		}
		else if (octaveMode == ArpOctaveMode::ALTERNATE && noteMode == ArpNoteMode::UP) {
			preset = ArpPreset::BOTH;
		}
		else if (octaveMode == ArpOctaveMode::RANDOM && noteMode == ArpNoteMode::RANDOM) {
			preset = ArpPreset::RANDOM;
		}
		else {
			preset = ArpPreset::CUSTOM;
		}
	}

	void updateSettingsFromCurrentPreset() {
		if (preset == ArpPreset::OFF) {
			mode = ArpMode::OFF;
		}
		else if (preset == ArpPreset::UP) {
			mode = ArpMode::ARP;
			octaveMode = ArpOctaveMode::UP;
			noteMode = ArpNoteMode::UP;
		}
		else if (preset == ArpPreset::DOWN) {
			mode = ArpMode::ARP;
			octaveMode = ArpOctaveMode::DOWN;
			noteMode = ArpNoteMode::DOWN;
		}
		else if (preset == ArpPreset::BOTH) {
			mode = ArpMode::ARP;
			octaveMode = ArpOctaveMode::ALTERNATE;
			noteMode = ArpNoteMode::UP;
		}
		else if (preset == ArpPreset::RANDOM) {
			mode = ArpMode::ARP;
			octaveMode = ArpOctaveMode::RANDOM;
			noteMode = ArpNoteMode::RANDOM;
		}
		else if (preset == ArpPreset::CUSTOM) {
			mode = ArpMode::ARP;
			// Although CUSTOM has octaveMode and noteMode freely setable, when we select CUSTOM from the preset menu
			// shortcut, we can provide here some default starting settings that user can change later with the menus.
			octaveMode = ArpOctaveMode::UP;
			noteMode = ArpNoteMode::UP;
		}
	}

	uint32_t getPhaseIncrement(int32_t arpRate);

	// Main settings
	ArpPreset preset{ArpPreset::OFF};
	ArpMode mode{ArpMode::OFF};

	// Sequence settings
	ArpOctaveMode octaveMode{ArpOctaveMode::UP};
	ArpNoteMode noteMode{ArpNoteMode::UP};

	// Octave settings
	uint8_t numOctaves{2};

	// Sync settings
	SyncLevel syncLevel;
	SyncType syncType;

	// Rhythm settings
	uint8_t rhythm{0};

	// MPE settings
	ArpMpeModSource mpeVelocity{ArpMpeModSource::OFF};
	ArpMpeModSource mpeOctaves{ArpMpeModSource::OFF};
	ArpMpeModSource mpeGate{ArpMpeModSource::OFF};
	ArpMpeModSource mpeRatchetAmount{ArpMpeModSource::OFF};
	ArpMpeModSource mpeRatchetProbability{ArpMpeModSource::OFF};

	// Temporary flags
	bool flagForceArpRestart{false};
};

struct ArpNote {
	int16_t inputCharacteristics[2]; // Before arpeggiation. And applying to MIDI input if that's happening. Or, channel
	                                 // might be MIDI_CHANNEL_NONE.
	int16_t mpeValues[kNumExpressionDimensions];
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
	uint32_t sampleSyncLengthOn; // This defaults to zero, or may be overwritten by the caller to the Arp - and then the
	                             // Arp itself may override that.
	ArpNote* arpNoteOn;

	// And these are only valid if doing a note-off
	uint8_t outputMIDIChannelOff; // For MPE
};

class ArpeggiatorBase {
public:
	virtual void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity,
	                    ArpReturnInstruction* instruction, int32_t fromMIDIChannel, int16_t const* mpeValues) = 0;
	void render(ArpeggiatorSettings* settings, int32_t numSamples, uint32_t gateThreshold, uint32_t phaseIncrement,
	            uint32_t sequenceLength, uint32_t ratchetAmount, uint32_t ratchetProbability,
	            ArpReturnInstruction* instruction);
	int32_t doTickForward(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, uint32_t ClipCurrentPos,
	                      bool currentlyPlayingReversed);
	virtual bool hasAnyInputNotesActive() = 0;
	virtual void reset() = 0;

	bool ratchetingIsAvailable = true;
	bool gateCurrentlyActive;
	uint32_t gatePos;
	int8_t currentOctave;
	int8_t currentDirection;
	int8_t currentOctaveDirection;
	bool playedFirstArpeggiatedNoteYet;
	uint8_t lastVelocity;
	int16_t noteCodeCurrentlyOnPostArp;
	uint8_t outputMIDIChannelForNoteCurrentlyOnPostArp;

	// Playing state
	uint32_t notesPlayedFromSequence = 0;
	uint32_t randomNotesPlayedFromOctave = 0;

	// Rhythm state
	uint32_t notesPlayedFromRhythm = 0;

	// Ratcheting state
	uint32_t ratchetNotesIndex = 0;
	uint32_t ratchetNotesMultiplier = 0;
	uint32_t ratchetNotesNumber = 0;
	bool isRatcheting = false;

	// Unpatched Automated Params
	uint16_t ratchetAmount = 0;
	uint16_t ratchetProbability = 0;
	uint32_t gate = 1 << 23; // Default Gate: 25 (0-50)

	uint32_t modulatedVelocity = 0; // This is the modulated velocity (once any MPE modulations are applied)
	uint32_t modulatedRatchetProbability =
	    0; // This is the modulated ratchet probability (once any MPE modulations are applied)
	uint32_t modulatedRatchetAmount = 0; // This is the modulated ratchet amount (once any MPE modulations are applied)
	uint32_t modulatedGate = gate;       // This is the modulated gate threshold (once any MPE modulations are applied)
	uint32_t modulatedOctaveOffset =
	    0; // This is the modulated octave number to offset the notes (once any MPE modulations are applied)

	uint32_t maxSequenceLength = 0;

protected:
	void resetRatchet();
	void resetRhythm();
	void carryOnOctaveSequenceForSingleNoteArpeggio(ArpeggiatorSettings* settings);
	void maybeSetupNewRatchet(ArpeggiatorSettings* settings);
	bool evaluateRhythm(ArpeggiatorSettings* settings, int32_t rhythmPatternIndex);
	uint32_t calculateSingleMpeModulatedParameter(ArpMpeModSource source, int16_t zPressure, int16_t ySlideTimbre,
	                                              int32_t baseValue, int32_t maxValue);
	void calculateAllMpeModulatedParameters(ArpeggiatorSettings* settings, int16_t zPressure, int16_t ySlideTimbre);
	int32_t getOctaveDirection(ArpeggiatorSettings* settings);
	virtual void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) = 0;
	void switchAnyNoteOff(ArpReturnInstruction* instruction);
};

class ArpeggiatorForDrum final : public ArpeggiatorBase {
public:
	ArpeggiatorForDrum();
	void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity, ArpReturnInstruction* instruction,
	            int32_t fromMIDIChannel, int16_t const* mpeValues);
	void noteOff(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction);
	void reset();
	ArpNote arpNote; // For the one note. noteCode will always be 60. velocity will be 0 if off.

protected:
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet);
	bool hasAnyInputNotesActive();
};

class Arpeggiator final : public ArpeggiatorBase {
public:
	Arpeggiator();

	void reset();

	void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity, ArpReturnInstruction* instruction,
	            int32_t fromMIDIChannel, int16_t const* mpeValues);
	void noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp, ArpReturnInstruction* instruction);
	bool hasAnyInputNotesActive();

	// This array tracks the notes ordered by noteCode
	OrderedResizeableArray notes;
	// This array tracks the notes as they were played by the user
	ResizeableArray notesAsPlayed;
	int16_t whichNoteCurrentlyOnPostArp; // As in, the index within our list

protected:
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet);
};
