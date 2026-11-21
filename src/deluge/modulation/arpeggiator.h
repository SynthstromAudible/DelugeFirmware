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

#include "algorithm"
#include "definitions_cxx.hpp"
#include "model/note/note_lock_playback.h"
#include "model/sync.h"
#include "storage/storage_manager.h"
#include "util/container/array/ordered_resizeable_array.h"
#include "util/container/array/resizeable_array.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

class PostArpTriggerable;
class ParamManagerForTimeline;
class ParamManager;

constexpr uint32_t RANDOMIZER_LOCK_MAX_SAVED_VALUES = 16;
constexpr uint32_t ARP_MAX_INSTRUCTION_NOTES = 4;
constexpr uint32_t PATTERN_MAX_BUFFER_SIZE = 16;

constexpr int16_t ARP_NOTE_NONE = 32767;

enum class ArpType : uint8_t {
	SYNTH,
	DRUM,
	KIT,
};

class ArpeggiatorSettings {
public:
	ArpeggiatorSettings();

	void updateParamsFromParamManager(ParamManager* param_manager);

	void cloneFrom(ArpeggiatorSettings const* other);

	bool readCommonTagsFromFile(Deserializer& reader, char const* tagName, Song* songToConvertSyncLevel);

	bool readNonAudioTagsFromFile(Deserializer& reader, char const* tagName);

	void writeCommonParamsToFile(Serializer& writer, Song* songToConvertSyncLevel);

	void writeNonAudioParamsToFile(Serializer& writer);

	void generateNewNotePattern();

	void updatePresetFromCurrentSettings();

	void updateSettingsFromCurrentPreset();

	uint32_t getPhaseIncrement(int32_t arpRate);

	// Main settings
	ArpPreset preset{ArpPreset::OFF};
	ArpMode mode{ArpMode::OFF};

	bool includeInKitArp{true};

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
	uint32_t lastLockedSwapProbabilityParameterValue{0};
	uint32_t lastLockedGlideProbabilityParameterValue{0};
	uint32_t lastLockedReverseProbabilityParameterValue{0};
	uint32_t lastLockedChordProbabilityParameterValue{0};
	uint32_t lastLockedRatchetProbabilityParameterValue{0};
	uint32_t lastLockedSpreadVelocityParameterValue{0};
	uint32_t lastLockedSpreadGateParameterValue{0};
	uint32_t lastLockedSpreadOctaveParameterValue{0};

	// Pre-calculated randomized values for each parameter
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedNoteProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedBassProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedSwapProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedGlideProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedReverseProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedChordProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedRatchetProbabilityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedSpreadVelocityValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedSpreadGateValues;
	std::array<int8_t, RANDOMIZER_LOCK_MAX_SAVED_VALUES> lockedSpreadOctaveValues;

	// Order for the pattern note mode
	std::array<int8_t, PATTERN_MAX_BUFFER_SIZE> notePattern;

	// Temporary flags
	bool flagForceArpRestart{false};

	// Automatable params
	int32_t rate{0}; // Default to 25 if not set in XML
	int32_t gate{0}; // Default to 25 if not set in XML
	uint32_t rhythm{0};
	uint32_t sequenceLength{0};
	uint32_t chordPolyphony{0};
	uint32_t ratchetAmount{0};
	uint32_t noteProbability{4294967295u}; // Default to 25 if not set in XML
	uint32_t bassProbability{0};
	uint32_t swapProbability{0};
	uint32_t glideProbability{0};
	uint32_t reverseProbability{0};
	uint32_t chordProbability{0};
	uint32_t ratchetProbability{0};
	uint32_t spreadVelocity{0};
	uint32_t spreadGate{0};
	uint32_t spreadOctave{0};
};
enum class ArpNoteStatus : uint8_t {
	OFF,
	PLAYING,
	/// A held note (in Arpeggiator::notes) the arp has played. If the arp is switched off while it's still held, the
	/// arp hands it back to be played as a plain note (Arpeggiator::takeNoteHeldThroughArpOff())
	ARPEGGIATED,
};

struct ArpNote {
	ArpNote() {
		outputMemberChannel.fill(MIDI_CHANNEL_NONE);
		noteCodeOnPostArp.fill(ARP_NOTE_NONE);
		noteStatus.fill(ArpNoteStatus::OFF);
	}
	void resetPostArpArrays() {
		outputMemberChannel.fill(MIDI_CHANNEL_NONE);
		noteCodeOnPostArp.fill(ARP_NOTE_NONE);
		noteStatus.fill(ArpNoteStatus::OFF);
	}
	int16_t inputCharacteristics[2]{}; // Before arpeggiation. And applying to MIDI input if that's happening. Or,
	                                   // channel might be MIDI_CHANNEL_NONE.
	int16_t mpeValues[kNumExpressionDimensions]{};
	uint8_t velocity{0};
	uint8_t baseVelocity{0};
	// Keep the stretch length with the note for when the arp hands it back (takeNoteHeldThroughArpOff())
	uint32_t sampleSyncLength{0};
	// Parameter locks of the sequenced note that put this note in the arp (note_locks::pending at its note-on), so
	// the notes the arp generates from it later carry them too
	deluge::note_locks::NoteRef locks;

	// For note-ons
	std::array<ArpNoteStatus, ARP_MAX_INSTRUCTION_NOTES> noteStatus{};
	std::array<uint8_t, ARP_MAX_INSTRUCTION_NOTES> outputMemberChannel{};
	std::array<int16_t, ARP_MAX_INSTRUCTION_NOTES> noteCodeOnPostArp{};
};

struct ArpJustNoteCode {
	int16_t noteCode; // Before arpeggiation
};

class ArpReturnInstruction {
public:
	ArpReturnInstruction() {
		sampleSyncLengthOn = 0;
		invertReversed = false;
		arpNoteOn = nullptr;
		outputMIDIChannelOff.fill(MIDI_CHANNEL_NONE);
		noteCodeOffPostArp.fill(ARP_NOTE_NONE);
		glideOutputMIDIChannelOff.fill(MIDI_CHANNEL_NONE);
		glideNoteCodeOffPostArp.fill(ARP_NOTE_NONE);
	}

	// These are only valid if doing a note-on, or when releasing the most recently played with the arp off when other
	// notes are still playing (e.g. for mono note priority)
	uint32_t sampleSyncLengthOn; // This defaults to zero, or may be overwritten by the caller to the Arp - and then
	                             // the Arp itself may override that.

	// If set to true by the arpeggiator, the reverse SAMPLE flag will be inverted for the next voices to be played
	// and restored back to normal afterwards
	bool invertReversed;

	ArpNote* arpNoteOn;

	// And these are only valid if doing a note-off
	std::array<uint8_t, ARP_MAX_INSTRUCTION_NOTES> outputMIDIChannelOff; // For MPE
	std::array<int16_t, ARP_MAX_INSTRUCTION_NOTES> noteCodeOffPostArp;
	std::array<uint8_t, ARP_MAX_INSTRUCTION_NOTES> glideOutputMIDIChannelOff; // For MPE
	std::array<int16_t, ARP_MAX_INSTRUCTION_NOTES> glideNoteCodeOffPostArp;
};

class ArpeggiatorBase {
public:
	virtual ~ArpeggiatorBase() = default;
	ArpeggiatorBase() {
		glideNoteCodeCurrentlyOnPostArp.fill(ARP_NOTE_NONE);
		outputMIDIChannelForGlideNoteCurrentlyOnPostArp.fill(0);
	}

	virtual void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity,
	                    ArpReturnInstruction* instruction, int32_t fromMIDIChannel, int16_t const* mpeValues) = 0;
	virtual void noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp, ArpReturnInstruction* instruction) = 0;
	/// With the arp switched off, hands back one held note it had been arpeggiating, to be played as a plain note:
	/// sets the instruction's note-on and returns true. Called from doTickForward()
	virtual bool takeNoteHeldThroughArpOff(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) {
		return false;
	}
	void render(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, int32_t numSamples,
	            uint32_t gateThreshold, uint32_t phaseIncrement);
	int32_t doTickForward(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, uint32_t ClipCurrentPos,
	                      bool currentlyPlayingReversed);
	void calculateRandomizerAmounts(ArpeggiatorSettings* settings);
	virtual bool hasAnyInputNotesActive() = 0;
	virtual void reset() = 0;
	virtual ArpType getArpType() = 0;
	/// A null settings means no arpeggiation, same as ArpMode::OFF. Everything that branches on "is the arp on" must
	/// agree on this.
	static bool arpIsOff(ArpeggiatorSettings* settings) {
		return (settings == nullptr) || settings->mode == ArpMode::OFF;
	}
	ArpNote active_note; // For the currently active note.

	std::array<int16_t, ARP_MAX_INSTRUCTION_NOTES> glideNoteCodeCurrentlyOnPostArp;
	std::array<uint8_t, ARP_MAX_INSTRUCTION_NOTES> outputMIDIChannelForGlideNoteCurrentlyOnPostArp;
	uint32_t gatePos = 0;
	uint8_t lastVelocity = 0;

protected:
	void calculateNextNoteAndOrOctave(ArpeggiatorSettings* settings, uint8_t numActiveNotes);
	void setInitialNoteAndOctave(ArpeggiatorSettings* settings, uint8_t numActiveNotes);
	void resetBase();
	void resetRatchet();
	void executeArpStep(ArpeggiatorSettings* settings, uint8_t numActiveNotes, bool isRatchet,
	                    uint32_t maxSequenceLength, uint32_t rhythm, bool* shouldCarryOnRhythmNote,
	                    bool* shouldPlayNote, bool* shouldPlayBassNote, bool* shouldPlayRandomStep,
	                    bool* shouldPlayReverseNote, bool* shouldPlayChordNote);
	void increasePatternIndexes(uint8_t numStepRepeats);
	void increaseSequenceIndexes(uint32_t maxSequenceLength, uint32_t rhythm);
	void maybeSetupNewRatchet(ArpeggiatorSettings* settings);
	bool evaluateRhythm(uint32_t rhythm, bool isRatchet);
	bool evaluateNoteProbability(bool isRatchet);
	bool evaluateBassProbability(bool isRatchet);
	bool evaluateSwapProbability(bool isRatchet);
	bool evaluateReverseProbability(bool isRatchet);
	bool evaluateChordProbability(bool isRatchet);
	uint32_t calculateSpreadVelocity(uint8_t velocity, int32_t spreadVelocityForCurrentStep);
	int32_t getOctaveDirection(ArpeggiatorSettings* settings);
	virtual void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) = 0;
	void switchAnyNoteOff(ArpReturnInstruction* instruction);
	bool getRandomProbabilityResult(uint32_t value);
	int8_t getRandomBipolarProbabilityAmount(uint32_t value);
	int8_t getRandomWeighted2BitsAmount(uint32_t value);

	bool gateCurrentlyActive = false;

	bool playedFirstArpeggiatedNoteYet = false;

	// Playing state
	uint32_t notesPlayedFromSequence = 0;
	uint32_t randomNotesPlayedFromOctave = 0;

	// Sequence state
	int16_t whichNoteCurrentlyOnPostArp; // As in, the index within our list
	int8_t currentOctave = 0;
	int8_t currentDirection = 1;
	int8_t currentOctaveDirection = 1;

	// Rhythm state
	uint32_t notesPlayedFromRhythm = 0;
	uint32_t lastNormalNotePlayedFromRhythm = 0;

	// Locked randomizer state
	uint32_t notesPlayedFromLockedRandomizer = 0;

	// Note probability state
	bool lastNormalNotePlayedFromNoteProbability = true;

	// Bass probability state
	bool lastNormalNotePlayedFromBassProbability = false;

	// Swap probability state
	bool lastNormalNotePlayedFromSwapProbability = false;

	// Reverse probability state
	bool lastNormalNotePlayedFromReverseProbability = false;

	// Chord probability state
	bool lastNormalNotePlayedFromChordProbability = false;

	// Step repeat state
	int32_t stepRepeatIndex = 0;

	// Ratcheting state
	uint32_t ratchetNotesIndex = 0;
	uint32_t ratchetNotesMultiplier = 0;
	uint32_t ratchetNotesCount = 0;
	bool isRatcheting = false;

	// Glide state
	bool glideOnNextNoteOff = false;

	// Calculated randomizer values
	bool isPlayNoteForCurrentStep = true;
	bool isPlayBassForCurrentStep = false;
	bool isPlayRandomStepForCurrentStep = false;
	bool isPlayReverseForCurrentStep = false;
	bool isPlayChordForCurrentStep = false;
	bool isPlayRatchetForCurrentStep = false;
	bool isPlayGlideForCurrentStep = false;
	int32_t spreadVelocityForCurrentStep = 0;
	int32_t spreadGateForCurrentStep = 0;
	int32_t spreadOctaveForCurrentStep = 0;
	bool resetLockedRandomizerValuesNextTime = false;
};

class ArpeggiatorForDrum : public ArpeggiatorBase {
public:
	~ArpeggiatorForDrum() override = default;
	ArpeggiatorForDrum();
	void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity, ArpReturnInstruction* instruction,
	            int32_t fromMIDIChannel, int16_t const* mpeValues) override;
	void noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp, ArpReturnInstruction* instruction) override;
	void reset() override;
	ArpType getArpType() override { return ArpType::DRUM; }
	int16_t noteForDrum{0};

	bool invertReversedFromKitArp{false};

protected:
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) override;
	bool hasAnyInputNotesActive() override;
};

class Arpeggiator : public ArpeggiatorBase {
public:
	virtual ~Arpeggiator() = default;
	Arpeggiator();

	void reset() override;
	ArpType getArpType() override { return ArpType::SYNTH; }

	void noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity, ArpReturnInstruction* instruction,
	            int32_t fromMIDIChannel, int16_t const* mpeValues) override;
	void noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp, ArpReturnInstruction* instruction) override;
	bool takeNoteHeldThroughArpOff(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) override;
	bool hasAnyInputNotesActive() override;
	// This array tracks the notes ordered by noteCode
	OrderedResizeableArray notes;
	// This array tracks the notes as they were played by the user
	ResizeableArray notesAsPlayed;
	// This array tracks the notes as ordered by the Pattern note mode
	ResizeableArray notesByPattern;

protected:
	void rearrangePatterntArpNotes(ArpeggiatorSettings* settings);
	void rebuildNotesByPatternIfStale();
	void switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) override;
	bool anyArpeggiated{false}; ///< Some note in `notes` may be ArpNoteStatus::ARPEGGIATED
	// notesByPattern is only read by switchNoteOn() in PATTERN note mode, so rearrangePatterntArpNotes() just marks it
	// stale and switchNoteOn() rebuilds it when it needs it. Rebuilding is one insert per held note, and it used to
	// happen on every note-on and note-off, arp on or off: 20 inserts per note for a 20-row kit, on every step. The
	// pattern is copied when the rebuild is requested, so the result is the same as an immediate rebuild even if the
	// settings' pattern (or the settings object) changes in between.
	bool notesByPatternStale{false};
	std::array<int8_t, PATTERN_MAX_BUFFER_SIZE> notePatternForRebuild{};
};

class ArpeggiatorForKit : public Arpeggiator {
public:
	ArpeggiatorForKit() = default;
	ArpType getArpType() override { return ArpType::KIT; }
	void removeDrumIndex(ArpeggiatorSettings* arpSettings, int32_t drumIndex);
};
