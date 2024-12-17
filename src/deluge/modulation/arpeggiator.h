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
#include "model/sync.h"
#include "util/container/array/ordered_resizeable_array.h"
#include "util/container/array/resizeable_array.h"

#include <array>
#include <cstdint>

class PostArpTriggerable;
class ParamManagerForTimeline;

#define RANDOMIZER_LOCK_MAX_SAVED_VALUES 16

class ArpeggiatorSettings {
public:
	ArpeggiatorSettings();

	void cloneFrom(ArpeggiatorSettings const* other) {
		preset = other->preset;
		mode = other->mode;
		octaveMode = other->octaveMode;
		noteMode = other->noteMode;
		chordTypeIndex = other->chordTypeIndex;
		numOctaves = other->numOctaves;
		numStepRepeats = other->numStepRepeats;
		randomizerLock = other->randomizerLock;
		syncType = other->syncType;
		syncLevel = other->syncLevel;
		mpeVelocity = other->mpeVelocity;
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

	// Number of repetitions of each step
	uint8_t numStepRepeats{1};

	// Chord type (only for kit arpeggiators)
	uint8_t chordTypeIndex{0};

	// Sync settings
	SyncLevel syncLevel;
	SyncType syncType;

	// Arp randomizer lock
	bool randomizerLock{false};

	// MPE settings
	ArpMpeModSource mpeVelocity{ArpMpeModSource::OFF};

	// Spread last lock
	uint32_t lastLockedNoteProbabilityParameterValue{0};
	uint32_t lastLockedBassProbabilityParameterValue{0};
	uint32_t lastLockedChordProbabilityParameterValue{0};
	uint32_t lastLockedRatchetProbabilityParameterValue{0};
	uint32_t lastLockedSpreadVelocityParameterValue{0};
	uint32_t lastLockedSpreadGateParameterValue{0};
	uint32_t lastLockedSpreadOctaveParameterValue{0};

	// Up to 16 pre-calculated randomized values for each parameter
	int8_t lockedNoteProbabilityValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};
	int8_t lockedBassProbabilityValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};
	int8_t lockedChordProbabilityValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};
	int8_t lockedRatchetProbabilityValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};
	int8_t lockedSpreadVelocityValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};
	int8_t lockedSpreadGateValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};
	int8_t lockedSpreadOctaveValues[RANDOMIZER_LOCK_MAX_SAVED_VALUES]{};

	// Temporary flags
	bool flagForceArpRestart{false};
};

struct ArpNote {
	int16_t inputCharacteristics[2]; // Before arpeggiation. And applying to MIDI input if that's happening. Or, channel
	                                 // might be MIDI_CHANNEL_NONE.
	int16_t mpeValues[kNumExpressionDimensions];
	uint8_t velocity;
	uint8_t baseVelocity;
	uint8_t outputMemberChannel;
};

#define ARP_NOTE_NONE 32767

class ArpReturnInstruction {
public:
	ArpReturnInstruction()
	    : noteCodeOffPostArp(ARP_NOTE_NONE), noteCodeOnPostArp(ARP_NOTE_NONE), sampleSyncLengthOn(0) {}
	int16_t noteCodeOffPostArp; // 32767 means none/no action
	int16_t noteCodeOnPostArp;  // 32767 means none/no action

	// These are only valid if doing a note-on, or when releasing the most recently played with the arp off when other
	// notes are still playing (e.g. for mono note priority)
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
	void updateParams(uint32_t rhythmValue, uint32_t sequenceLength, uint32_t chordPoly, uint32_t ratchAmount,
	                  uint32_t noteProb, uint32_t bassProb, uint32_t chordProb, uint32_t ratchProb, uint32_t spVelocity,
	                  uint32_t spGate, uint32_t spOctave);
	void render(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, int32_t numSamples,
	            uint32_t gateThreshold, uint32_t phaseIncrement, uint32_t rhythmValue, uint32_t sequenceLength,
	            uint32_t chordPoly, uint32_t ratchAmount, uint32_t noteProb, uint32_t bassProb, uint32_t chordProb,
	            uint32_t ratchProb, uint32_t spVelocity, uint32_t spGate, uint32_t spOctave);
	int32_t doTickForward(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, uint32_t ClipCurrentPos,
	                      bool currentlyPlayingReversed);
	void calculateRandomizerAmounts(ArpeggiatorSettings* settings);
	virtual bool hasAnyInputNotesActive() = 0;
	virtual void reset() = 0;

	bool gateCurrentlyActive = false;
	uint32_t gatePos = 0;
	int8_t currentOctave = 0;
	int8_t currentDirection = 1;
	int8_t currentOctaveDirection = 1;
	bool playedFirstArpeggiatedNoteYet = false;
	uint8_t lastVelocity = 0;
	int16_t noteCodeCurrentlyOnPostArp = 0;
	uint8_t outputMIDIChannelForNoteCurrentlyOnPostArp = 0;

	// Playing state
	uint32_t notesPlayedFromSequence = 0;
	uint32_t randomNotesPlayedFromOctave = 0;
	int16_t whichNoteCurrentlyOnPostArp; // As in, the index within our list

	// Rhythm state
	uint32_t notesPlayedFromRhythm = 0;
	uint32_t lastNormalNotePlayedFromRhythm = 0;

	// Locked randomizer state
	uint32_t notesPlayedFromLockedRandomizer = 0;
	uint32_t lastNormalNotePlayedFromLockedRandomizer = 0;

	// Note probability state
	bool lastNormalNotePlayedFromNoteProbability = true;

	// Bass probability state
	bool lastNormalNotePlayedFromBassProbability = true;

	// Chord probability state
	bool lastNormalNotePlayedFromChordProbability = true;

	// Step repeat state
	int32_t stepRepeatIndex = 0;

	// Ratcheting state
	uint32_t ratchetNotesIndex = 0;
	uint32_t ratchetNotesMultiplier = 0;
	uint32_t ratchetNotesCount = 0;
	bool isRatcheting = false;

	// Chord state
	uint32_t chordNotesCount = 0;

	// Calculated spread amounts
	bool isPlayNoteForCurrentStep = true;
	bool isPlayBassForCurrentStep = false;
	bool isPlayChordForCurrentStep = false;
	bool isPlayRatchetForCurrentStep = false;
	int32_t spreadVelocityForCurrentStep = 0;
	int32_t spreadGateForCurrentStep = 0;
	int32_t spreadOctaveForCurrentStep = 0;
	bool resetLockedRandomizerValuesNextTime = false;

	// Unpatched Automated Params
	uint32_t noteProbability = 0;
	uint32_t bassProbability = 0;
	uint32_t chordProbability = 0;
	uint32_t ratchetProbability = 0;
	uint32_t maxSequenceLength = 0;
	uint32_t rhythm = 0;
	uint32_t chordPolyphony = 0;
	uint32_t ratchetAmount = 0;
	uint32_t spreadVelocity = 0;
	uint32_t spreadGate = 0;
	uint32_t spreadOctave = 0;

protected:
	void calculateNextNoteAndOrOctave(ArpeggiatorSettings* settings, uint8_t numActiveNotes);
	void setInitialNoteAndOctave(ArpeggiatorSettings* settings, uint8_t numActiveNotes);
	void resetBase();
	void resetRatchet();
	void carryOnOctaveSequence(ArpeggiatorSettings* settings);
	void increaseIndexes(bool hasPlayedRhythmNote, uint8_t numStepRepeats);
	void maybeSetupNewRatchet(ArpeggiatorSettings* settings);
	bool evaluateRhythm(bool isRatchet);
	bool evaluateNoteProbability(bool isRatchet);
	bool evaluateBassProbability(bool isRatchet);
	bool evaluateChordProbability(bool isRatchet);
	int32_t getOctaveDirection(ArpeggiatorSettings* settings);
	virtual void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) = 0;
	void switchAnyNoteOff(ArpReturnInstruction* instruction);
	bool getRandomProbabilityResult(ArpeggiatorSettings* settings, uint32_t value);
	int8_t getRandomUnipolarProbabilityAmount(ArpeggiatorSettings* settings, uint32_t value);
	int8_t getRandomBipolarProbabilityAmount(ArpeggiatorSettings* settings, uint32_t value);
	int8_t getRandomWeightedFourAmount(ArpeggiatorSettings* settings, uint32_t value);
};

class ArpeggiatorForDrum final : public ArpeggiatorBase {
public:
	ArpeggiatorForDrum();
	void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity, ArpReturnInstruction* instruction,
	            int32_t fromMIDIChannel, int16_t const* mpeValues);
	void noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp, ArpReturnInstruction* instruction);
	void reset();
	ArpNote arpNote; // For the one note. noteCode will always be 60. velocity will be 0 if off.
	int16_t noteForDrum;

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

protected:
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet);
};
