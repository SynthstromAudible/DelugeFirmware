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

#include "modulation/arpeggiator.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/value_scaling.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/arpeggiator_rhythms.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"
#include "util/functions.h"
#include <cstdint>

#define MIN_MPE_MODULATED_VELOCITY 10

namespace params = deluge::modulation::params;

ArpeggiatorSettings::ArpeggiatorSettings() {

	// I'm so sorry, this is incredibly ugly, but in order to decide the default sync level, we have to look at the
	// current song, or even better the one being preloaded. Default sync level is used obviously for the default synth
	// sound if no SD card inserted, but also some synth presets, possibly just older ones, are saved without this so it
	// can be set to the default at the time of loading.
	Song* song = preLoadedSong;
	if (!song) {
		song = currentSong;
	}
	if (song) {
		syncLevel = (SyncLevel)(8 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM));
	}
	else {
		syncLevel = (SyncLevel)(8 - FlashStorage::defaultMagnitude);
	}
	syncType = SYNC_TYPE_EVEN;

	lockedNoteProbabilityValues.fill(0);
	lockedBassProbabilityValues.fill(0);
	lockedChordProbabilityValues.fill(0);
	lockedRatchetProbabilityValues.fill(0);
	lockedReverseProbabilityValues.fill(0);
	lockedSpreadVelocityValues.fill(0);
	lockedSpreadGateValues.fill(0);
	lockedSpreadOctaveValues.fill(0);

	generateNewNotePattern();
}

ArpeggiatorForDrum::ArpeggiatorForDrum() {
	arpNote.velocity = 0;
}

Arpeggiator::Arpeggiator()
    : notes(sizeof(ArpNote), 16, 0, 8, 8), notesAsPlayed(sizeof(ArpJustNoteCode), 8, 8),
      notesByPattern(sizeof(ArpJustNoteCode), 8, 8) {
	notes.emptyingShouldFreeMemory = false;
	notesAsPlayed.emptyingShouldFreeMemory = false;
	notesByPattern.emptyingShouldFreeMemory = false;
}

void Arpeggiator::reset() {
	notes.empty();
	notesAsPlayed.empty();
	notesByPattern.empty();
}

// Surely this shouldn't be quite necessary?
void ArpeggiatorForDrum::reset() {
	arpNote.velocity = 0;
}

void ArpeggiatorBase::resetRatchet() {
	// Ratchets
	isRatcheting = false;
	ratchetNotesMultiplier = 0;
	ratchetNotesCount = 0;
	ratchetNotesIndex = 0;
}
void ArpeggiatorBase::resetBase() {
	// Ratchets
	resetRatchet();
	// Rhythm
	notesPlayedFromRhythm = 0;
	lastNormalNotePlayedFromRhythm = 0;
	// Step repeat
	stepRepeatIndex = 0;
}

void ArpeggiatorForDrum::noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t originalVelocity,
                                ArpReturnInstruction* instruction, int32_t fromMIDIChannel, int16_t const* mpeValues) {
	lastVelocity = originalVelocity;
	noteForDrum = noteCode;

	bool wasActiveBefore = arpNote.velocity;

	arpNote.inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = noteCode;
	arpNote.inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = fromMIDIChannel;
	arpNote.baseVelocity = originalVelocity;
	arpNote.velocity = originalVelocity; // Means note is on.

	// MIDIInstrument might set this later, but it needs to be MIDI_CHANNEL_NONE until then so it doesn't get included
	// in the survey that will happen of existing output member channels.
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		arpNote.outputMemberChannel[n] = MIDI_CHANNEL_NONE;
	}
	// Update expression values
	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		arpNote.mpeValues[m] = mpeValues[m];
	}

	// If we're an actual arpeggiator...
	if ((settings != nullptr) && settings->mode != ArpMode::OFF) {

		// If this was the first note-on and we want to sound a note right now...
		if (!wasActiveBefore) {
			playedFirstArpeggiatedNoteYet = false;
			gateCurrentlyActive = false;

			if (!(playbackHandler.isEitherClockActive()) || !settings->syncLevel) {
				switchNoteOn(settings, instruction, false);
			}
		}

		// Don't do the note-on now, it'll happen automatically at next render
	}

	// Or otherwise, just switch the note on.
	else {
		// Apply randomizer
		calculateRandomizerAmounts(settings);
		notesPlayedFromLockedRandomizer = (notesPlayedFromLockedRandomizer + 1) % RANDOMIZER_LOCK_MAX_SAVED_VALUES;

		if (isPlayNoteForCurrentStep) {
			// Play a note

			arpNote.baseVelocity = originalVelocity;
			// Now apply velocity spread to the base velocity
			uint8_t velocity = calculateSpreadVelocity(originalVelocity, spreadVelocityForCurrentStep);
			arpNote.velocity = velocity; // calculated modified velocity

			// Set the note to be played
			noteCodeCurrentlyOnPostArp[0] = noteCode;
			arpNote.noteCodeOnPostArp[0] = noteCode;
			for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				// Clean rest of slots
				noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
				arpNote.noteCodeOnPostArp[n] = ARP_NOTE_NONE;
			}
			instruction->invertReversed = isPlayReverseForCurrentStep;
			instruction->arpNoteOn = &arpNote;
		}
	}
}

void ArpeggiatorForDrum::noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp,
                                 ArpReturnInstruction* instruction) {

	// If no arpeggiation...
	if ((settings == nullptr) || settings->mode == ArpMode::OFF) {
		instruction->noteCodeOffPostArp[0] = noteCodePreArp;
		instruction->outputMIDIChannelOff[0] = arpNote.outputMemberChannel[0];
		noteCodeCurrentlyOnPostArp[0] = ARP_NOTE_NONE;
		outputMIDIChannelForNoteCurrentlyOnPostArp[0] = MIDI_CHANNEL_NONE;
		for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			// If no arp, rest of chord notes are for sure disabled
			instruction->noteCodeOffPostArp[n] = ARP_NOTE_NONE;
			instruction->outputMIDIChannelOff[n] = MIDI_CHANNEL_NONE;
			noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
			outputMIDIChannelForNoteCurrentlyOnPostArp[n] = MIDI_CHANNEL_NONE;
		}
	}

	// Or if yes arpeggiation...
	else {
		if (gateCurrentlyActive) {
			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				// Set all chord notes
				instruction->noteCodeOffPostArp[n] = noteCodeCurrentlyOnPostArp[n];
				instruction->outputMIDIChannelOff[n] = outputMIDIChannelForNoteCurrentlyOnPostArp[n];
				// Clean the temp state
				noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
				outputMIDIChannelForNoteCurrentlyOnPostArp[n] = MIDI_CHANNEL_NONE;
			}
		}
	}

	arpNote.velocity = 0; // Means note is off
}

// May return the instruction for a note-on, or no instruction. The noteCode instructed might be some octaves up from
// that provided here.
void Arpeggiator::noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t originalVelocity,
                         ArpReturnInstruction* instruction, int32_t fromMIDIChannel, int16_t const* mpeValues) {
	lastVelocity = originalVelocity;

	bool noteExists = false;

	ArpNote* arpNote;

	int32_t notesKey = notes.search(noteCode, GREATER_OR_EQUAL);
	if (notesKey < notes.getNumElements()) [[unlikely]] {
		arpNote = (ArpNote*)notes.getElementAddress(notesKey);
		if (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCode) {
			noteExists = true;
		}
	}

	if (noteExists) {
		// If note exists already, do nothing if we are an arpeggiator, and if not, go to noteinserted to update
		// midiChannel
		if ((settings != nullptr) && settings->mode != ArpMode::OFF) {
			return; // If we're an arpeggiator, return
		}
		else {
			goto noteInserted;
		}
	}
	// If note does not exist yet in the arrays, we must insert it in both
	else {

		// ORDERED NOTES

		// Insert it in notes array
		Error error = notes.insertAtIndex(notesKey);
		if (error != Error::NONE) {
			return;
		}
		// Save arpNote
		arpNote = (ArpNote*)notes.getElementAddress(notesKey);
		arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = noteCode;
		arpNote->baseVelocity = originalVelocity;
		arpNote->velocity = originalVelocity;

		// MIDIInstrument might set this, but it needs to be MIDI_CHANNEL_NONE until then so it
		// doesn't get included in the survey that will happen of existing output member
		// channels.
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			arpNote->outputMemberChannel[n] = MIDI_CHANNEL_NONE;
		}
		// Update expression values
		for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
			arpNote->mpeValues[m] = mpeValues[m];
		}

		// "PLAYED ORDER" NOTES

		// Insert it in notesAsPlayed array
		int32_t notesAsPlayedIndex = notesAsPlayed.getNumElements();
		error = notesAsPlayed.insertAtIndex(notesAsPlayedIndex); // always insert at the end or the array
		if (error != Error::NONE) {
			return;
		}
		// Save arpNote
		ArpJustNoteCode* arpAsPlayedNote = (ArpJustNoteCode*)notesAsPlayed.getElementAddress(notesAsPlayedIndex);
		arpAsPlayedNote->noteCode = noteCode;

		// "PATTERN" NOTES

		rearrangePatterntArpNotes(settings);
	}

noteInserted:
	// This is here so that "stealing" a note being edited can then replace its MPE data during
	// editing. Kind of a hacky solution, but it works for now.
	arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = fromMIDIChannel;

	// If we're an arpeggiator...
	if ((settings != nullptr) && settings->mode != ArpMode::OFF) {

		// If this was the first note-on and we want to sound a note right now...
		if (notes.getNumElements() == 1) {
			playedFirstArpeggiatedNoteYet = false;
			gateCurrentlyActive = false;

			if (!(playbackHandler.isEitherClockActive()) || !settings->syncLevel) {
				switchNoteOn(settings, instruction, false);
			}
		}

		// Or if the arpeggiator was already sounding
		else {
			if (whichNoteCurrentlyOnPostArp >= notesKey) {
				whichNoteCurrentlyOnPostArp++;
			}
		}
		// Don't do the note-on now, it'll happen automatically at next render
	}
	else {
		// Apply randomizer
		calculateRandomizerAmounts(settings);
		notesPlayedFromLockedRandomizer = (notesPlayedFromLockedRandomizer + 1) % RANDOMIZER_LOCK_MAX_SAVED_VALUES;

		if (isPlayNoteForCurrentStep) {
			// Play a note

			arpNote->baseVelocity = originalVelocity;
			// Now apply velocity spread to the base velocity
			uint8_t velocity = calculateSpreadVelocity(originalVelocity, spreadVelocityForCurrentStep);
			arpNote->velocity = velocity; // calculated modified velocity

			// Set the note to be played
			noteCodeCurrentlyOnPostArp[0] = noteCode;
			arpNote->noteCodeOnPostArp[0] = noteCode;
			for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				// Clean rest of chord note slots
				noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
				arpNote->noteCodeOnPostArp[n] = ARP_NOTE_NONE;
			}
			instruction->invertReversed = isPlayReverseForCurrentStep;
			instruction->arpNoteOn = arpNote;
		}
	}
}

void Arpeggiator::noteOff(ArpeggiatorSettings* settings, int32_t noteCodePreArp, ArpReturnInstruction* instruction) {
	int32_t notesKey = notes.search(noteCodePreArp, GREATER_OR_EQUAL);
	if (notesKey < notes.getNumElements()) {

		ArpNote* arpNote = (ArpNote*)notes.getElementAddress(notesKey);
		if (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCodePreArp) {
			bool arpOff = (settings == nullptr) || settings->mode == ArpMode::OFF;
			// If no arpeggiation...
			if (arpOff) {
				instruction->noteCodeOffPostArp[0] = noteCodePreArp;
				instruction->outputMIDIChannelOff[0] = arpNote->outputMemberChannel[0];
				noteCodeCurrentlyOnPostArp[0] = ARP_NOTE_NONE;
				outputMIDIChannelForNoteCurrentlyOnPostArp[0] = MIDI_CHANNEL_NONE;
				for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
					// If no arp, rest of chord notes are for sure disabled
					instruction->noteCodeOffPostArp[n] = ARP_NOTE_NONE;
					instruction->outputMIDIChannelOff[n] = MIDI_CHANNEL_NONE;
					noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
					outputMIDIChannelForNoteCurrentlyOnPostArp[n] = MIDI_CHANNEL_NONE;
				}
			}

			// Or if yes arpeggiation, we'll only stop right now if that was the last note to switch off. Otherwise,
			// it'll turn off soon with the arpeggiation.
			else {
				if (notes.getNumElements() == 1) {
					if (whichNoteCurrentlyOnPostArp == notesKey && gateCurrentlyActive) {
						for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
							// Set all chord notes
							instruction->noteCodeOffPostArp[n] = noteCodeCurrentlyOnPostArp[n];
							instruction->outputMIDIChannelOff[n] = outputMIDIChannelForNoteCurrentlyOnPostArp[n];
							noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
							outputMIDIChannelForNoteCurrentlyOnPostArp[n] = MIDI_CHANNEL_NONE;
						}
					}
				}
			}
			notes.deleteAtIndex(notesKey);

			// We must also search and delete from notesAsPlayed
			int numNotes = notesAsPlayed.getNumElements();
			for (int32_t i = 0; i < numNotes; i++) {
				ArpJustNoteCode* arpAsPlayedNote = (ArpJustNoteCode*)notesAsPlayed.getElementAddress(i);
				if (arpAsPlayedNote->noteCode == noteCodePreArp) {
					notesAsPlayed.deleteAtIndex(i);
					if (arpOff && i == numNotes - 1 && i > 0) {
						// if we're not arpeggiating then pass the second last note back - cv instruments will snap back
						// to it (like playing a normal mono synth)
						ArpJustNoteCode* lastArpAsPlayedNote = (ArpJustNoteCode*)notesAsPlayed.getElementAddress(i - 1);
						int32_t newNotesKey = notes.search(lastArpAsPlayedNote->noteCode, GREATER_OR_EQUAL);
						if (newNotesKey < notes.getNumElements()) [[likely]] {
							ArpNote* lastArpNote = (ArpNote*)notes.getElementAddress(newNotesKey);
							lastArpNote->noteCodeOnPostArp[0] =
							    lastArpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];
							for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
								// Clean rest of chord note slots
								lastArpNote->noteCodeOnPostArp[n] = ARP_NOTE_NONE;
							}
							instruction->arpNoteOn = lastArpNote;
						}
					}
					break;
				}
			}

			// We must also rearrange notesByPattern as the note order may have changed completely
			rearrangePatterntArpNotes(settings);

			if (whichNoteCurrentlyOnPostArp >= notesKey) {
				whichNoteCurrentlyOnPostArp--; // Beware - this could send it negative
				if (whichNoteCurrentlyOnPostArp < 0) {
					whichNoteCurrentlyOnPostArp = 0;
				}
			}

			if (isRatcheting && (ratchetNotesIndex >= ratchetNotesCount || !playbackHandler.isEitherClockActive())) {
				// If it was ratcheting but it reached the last note in the ratchet or play was stopped
				// then we can reset the ratchet temp values in advance
				resetRatchet();
			}
		}
	}

	if (notes.getNumElements() == 0) {
		// Playback was stopped, or, at least, all notes have been released
		// so we can reset the ratchet temp values and the rhythm temp values
		resetBase();
		playedFirstArpeggiatedNoteYet = false;
	}
}

void ArpeggiatorBase::switchAnyNoteOff(ArpReturnInstruction* instruction) {
	if (gateCurrentlyActive) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			// Set all chord notes
			instruction->noteCodeOffPostArp[n] = noteCodeCurrentlyOnPostArp[n];
			instruction->outputMIDIChannelOff[n] = outputMIDIChannelForNoteCurrentlyOnPostArp[n];
			// Clean the temp state
			noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
			outputMIDIChannelForNoteCurrentlyOnPostArp[n] = MIDI_CHANNEL_NONE;
		}
		gateCurrentlyActive = false;
	}
}

// Sets up the ratchet state if the probability is met
void ArpeggiatorBase::maybeSetupNewRatchet(ArpeggiatorSettings* settings) {
	isRatcheting = isPlayRatchetForCurrentStep
	               && settings->ratchetAmount > 0
	               // For the highest sync we can't divide the time any more, so not possible to ratchet
	               && !(settings->syncType == SYNC_TYPE_EVEN && settings->syncLevel == SyncLevel::SYNC_LEVEL_256TH);
	if (isRatcheting) {
		ratchetNotesMultiplier = getRandomWeighted2BitsAmount(settings->ratchetAmount);
		ratchetNotesCount = 1 << ratchetNotesMultiplier;
		if (settings->syncLevel == SyncLevel::SYNC_LEVEL_128TH) {
			// If the sync level is 128th, we can't have a ratchet of more than 2 notes, so we set it to the minimum
			ratchetNotesMultiplier = 1;
			ratchetNotesCount = 2;
		}
		else if (settings->syncLevel == SyncLevel::SYNC_LEVEL_64TH) {
			// If the sync level is 64th, the maximum ratchet can be of 4 notes (8 not allowed)
			ratchetNotesMultiplier = std::max(2_u32, ratchetNotesMultiplier);
			ratchetNotesCount = std::max(4_u32, ratchetNotesCount);
		}
	}
	else {
		ratchetNotesMultiplier = 0;
		ratchetNotesCount = 0;
	}
	ratchetNotesIndex = 0;
}

uint32_t ArpeggiatorBase::calculateSpreadVelocity(uint8_t velocity, int32_t spreadVelocityForCurrentStep) {
	if (spreadVelocityForCurrentStep == 0) {
		// No spreading
		return velocity;
	}
	int32_t signedVelocity = (int32_t)velocity;
	int32_t diff = 0;
	if (spreadVelocityForCurrentStep < 0) {
		// Reducing velocity
		diff = -(multiply_32x32_rshift32((-spreadVelocityForCurrentStep) << 24, signedVelocity - 1) << 1);
	}
	else {
		// Increasing velocity
		diff = (multiply_32x32_rshift32(spreadVelocityForCurrentStep << 24, 127 - signedVelocity) << 1);
	}
	signedVelocity = signedVelocity + diff;
	// And fix it if out of bounds
	if (signedVelocity < 1) {
		signedVelocity = 1;
	}
	else if (signedVelocity > 127) {
		signedVelocity = 127;
	}
	// Convert back to unsigned
	return (uint32_t)signedVelocity;
}

// Returns if the arpeggiator should advance to next note or not
bool ArpeggiatorBase::evaluateRhythm(uint32_t rhythm, bool isRatchet) {
	// If it is a rachet, use the last normal note played index, but it it is not a ratchet, play the new rhythm index
	int32_t rhythmPatternIndex = isRatchet ? lastNormalNotePlayedFromRhythm : notesPlayedFromRhythm;
	int32_t numberOfRhythmSteps = arpRhythmPatterns[rhythm].length;
	rhythmPatternIndex = rhythmPatternIndex % numberOfRhythmSteps; // normalize the index just in case
	return arpRhythmPatterns[rhythm].steps[rhythmPatternIndex];
}

// Returns if the arpeggiator should play the next note or not
bool ArpeggiatorBase::evaluateNoteProbability(bool isRatchet) {
	// If it is a rachet, use the last value, but it it is not a ratchet, use the calculated value
	return isRatchet ? lastNormalNotePlayedFromNoteProbability : isPlayNoteForCurrentStep;
}

// Returns if the arpeggiator should play the bass note instead of the normal note
bool ArpeggiatorBase::evaluateBassProbability(bool isRatchet) {
	// If it is a rachet, use the last value, but it it is not a ratchet, use the calculated value
	return isRatchet ? lastNormalNotePlayedFromBassProbability : isPlayBassForCurrentStep;
}

// Returns if the arpeggiator should play the bass note instead of the normal note
bool ArpeggiatorBase::evaluateReverseProbability(bool isRatchet) {
	// If it is a rachet, use the last value, but it it is not a ratchet, use the calculated value
	return isRatchet ? lastNormalNotePlayedFromReverseProbability : isPlayReverseForCurrentStep;
}

// Returns if the arpeggiator should play a chord instead of the normal note
bool ArpeggiatorBase::evaluateChordProbability(bool isRatchet) {
	// If it is a rachet, use the last value, but it it is not a ratchet, use the calculated value
	return isRatchet ? lastNormalNotePlayedFromChordProbability : isPlayChordForCurrentStep;
}

// Returns if note should be played
void ArpeggiatorBase::executeArpStep(ArpeggiatorSettings* settings, uint8_t numActiveNotes, bool isRatchet,
                                     uint32_t maxSequenceLength, uint32_t rhythm, bool shouldCarryOnRhythmNote,
                                     bool shouldPlayNote, bool shouldPlayBassNote, bool shouldPlayReverseNote,
                                     bool shouldPlayChordNote) {

	// Here we reset the arpeggiator sequence based on several possible conditions
	if (settings->flagForceArpRestart) {
		// If flagged to restart sequence, do it now and reset the flag
		playedFirstArpeggiatedNoteYet = false;
		settings->flagForceArpRestart = false;
	}
	if (!isRatchet
	    && (!playedFirstArpeggiatedNoteYet
	        || (maxSequenceLength > 0 && notesPlayedFromSequence >= maxSequenceLength))) {
		// Reset first note flag
		playedFirstArpeggiatedNoteYet = false;
		// Reset indexes
		notesPlayedFromSequence = 0;
		notesPlayedFromRhythm = 0;
		lastNormalNotePlayedFromRhythm = 0;
		notesPlayedFromLockedRandomizer = 0;
		randomNotesPlayedFromOctave = 0;
		stepRepeatIndex = 0;
		whichNoteCurrentlyOnPostArp = 0;
	}

	if (isRatchet) {
		// Increment ratchet note index if we are ratcheting
		ratchetNotesIndex++;
	}
	else {
		// If this is a normal note event (not ratchet) we take here the opportunity to setup a
		// ratchet burst and also make all the necessary calculations for the next note to be played

		if (shouldCarryOnRhythmNote) {
			// Setup ratchet
			maybeSetupNewRatchet(settings);

			// If which-note not actually set up yet...
			if (!playedFirstArpeggiatedNoteYet) {
				// Set the initial note and octave values and directions
				setInitialNoteAndOctave(settings, numActiveNotes);
			}
			else {
				// Move indexes to the next note in the sequence, according to OctaveMode, NoteMode and StepRepeat
				calculateNextNoteAndOrOctave(settings, numActiveNotes);
			}

			// Increase pattern related indexes
			increasePatternIndexes(settings->numStepRepeats);

			// As we have set a note and an octave, we can say we have played a note
			playedFirstArpeggiatedNoteYet = true;

			// Save last note played from rhythm
			lastNormalNotePlayedFromRhythm = notesPlayedFromRhythm;

			// Save last note played from probability
			lastNormalNotePlayedFromNoteProbability = shouldPlayNote;

			// Save last note played from probability
			lastNormalNotePlayedFromBassProbability = shouldPlayBassNote;

			// Save last note played from probability
			lastNormalNotePlayedFromReverseProbability = shouldPlayReverseNote;

			// Save last note played from probability
			lastNormalNotePlayedFromChordProbability = shouldPlayChordNote;
		}

		// Increase steps played from the sequence or rhythm for both silent and non-silent notes
		increaseSequenceIndexes(maxSequenceLength, rhythm);
	}
}

// Increase random, sequence, rhythm and spread indexes
void ArpeggiatorBase::increasePatternIndexes(uint8_t numStepRepeats) {
	// Randome Notes from octave
	randomNotesPlayedFromOctave++;

	// Randomizer
	notesPlayedFromLockedRandomizer = (notesPlayedFromLockedRandomizer + 1) % RANDOMIZER_LOCK_MAX_SAVED_VALUES;

	// Step repeat
	stepRepeatIndex++;
	if (stepRepeatIndex >= numStepRepeats) {
		stepRepeatIndex = 0;
	}
}

// Increase random, sequence, rhythm and spread indexes
void ArpeggiatorBase::increaseSequenceIndexes(uint32_t maxSequenceLength, uint32_t rhythm) {
	// Sequence Length
	if (maxSequenceLength > 0) {
		notesPlayedFromSequence++;
	}
	// Rhythm
	notesPlayedFromRhythm = (notesPlayedFromRhythm + 1) % arpRhythmPatterns[rhythm].length;
}

void ArpeggiatorForDrum::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction,
                                      bool isRatchet) {
	// Get params
	uint32_t maxSequenceLength = computeCurrentValueForUnsignedMenuItem(settings->sequenceLength);
	uint32_t rhythm = computeCurrentValueForUnsignedMenuItem(settings->rhythm);

	// Probabilities
	bool shouldCarryOnRhythmNote = evaluateRhythm(rhythm, isRatchet);
	if (shouldCarryOnRhythmNote) {
		// Calculate randomizer amounts only for non-silent notes
		calculateRandomizerAmounts(settings);
	}
	bool shouldPlayNote = evaluateNoteProbability(isRatchet);
	bool shouldPlayBassNote = evaluateBassProbability(isRatchet);
	bool shouldPlayReverseNote = evaluateReverseProbability(isRatchet);

	// Execute all the step calculations
	executeArpStep(settings, chordTypeNoteCount[settings->chordTypeIndex], isRatchet, maxSequenceLength, rhythm,
	               shouldCarryOnRhythmNote, shouldPlayNote, shouldPlayBassNote, shouldPlayReverseNote, false);

	if (shouldCarryOnRhythmNote && shouldPlayNote) {
		// Set Gate as active
		gateCurrentlyActive = true;
		if (!isRatchet) {
			// Reset gate position (only for normal notes)
			gatePos = 0;
		}

		uint8_t velocity = arpNote.baseVelocity;

		if (settings->mpeVelocity != ArpMpeModSource::OFF) {
			// Check if we need to update velocity with some MPE value
			switch (settings->mpeVelocity) {
			case ArpMpeModSource::AFTERTOUCH:
				velocity = arpNote.mpeValues[util::to_underlying(Expression::Z_PRESSURE)] >> 8;
				break;
			case ArpMpeModSource::MPE_Y:
				velocity = arpNote.mpeValues[util::to_underlying(Expression::Y_SLIDE_TIMBRE)] >> 8;
				// velocity = ((arpNote.mpeValues[util::to_underlying(Expression::Y_SLIDE_TIMBRE)] >> 1) + (1 << 14)) >>
				// 8;
				break;
			// explicit fallthrough case
			case ArpMpeModSource::OFF:;
			}
			// Fix base velocity if it's outside of MPE-controlled bounds
			if (velocity < MIN_MPE_MODULATED_VELOCITY) {
				velocity = MIN_MPE_MODULATED_VELOCITY;
			}
		}
		arpNote.baseVelocity = velocity;
		// Now apply velocity spread to the base velocity
		velocity = calculateSpreadVelocity(velocity, spreadVelocityForCurrentStep);
		arpNote.velocity = velocity;
		// Get current sequence note
		int16_t note;
		if (shouldPlayBassNote) {
			note = noteForDrum;
		}
		else {
			int16_t diff = (int16_t)currentOctave * 12;
			if (spreadOctaveForCurrentStep != 0) {
				// Now apply octave spread to the base note
				diff = diff + spreadOctaveForCurrentStep * 12;
			}
			note = noteForDrum
			       + chordTypeSemitoneOffsets[settings->chordTypeIndex][whichNoteCurrentlyOnPostArp % MAX_CHORD_NOTES]
			       + diff;
		}
		// And fix it if out of bounds
		if (note < 0) {
			note = 0;
		}
		else if (note > 127) {
			note = 127;
		}
		// Set the note to be played
		// (Only one note, chord polyphony/probability is not available in kit rows)
		noteCodeCurrentlyOnPostArp[0] = note;
		arpNote.noteCodeOnPostArp[0] = noteCodeCurrentlyOnPostArp[0];
		for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			// Clean rest of chord note slots
			noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
			arpNote.noteCodeOnPostArp[n] = ARP_NOTE_NONE;
		}
		instruction->invertReversed = shouldPlayReverseNote;
		instruction->arpNoteOn = &arpNote;
	}
}

bool ArpeggiatorBase::getRandomProbabilityResult(uint32_t value) {
	if (value == 0) {
		return false; // If value at min probability, return always false
	}
	if (value == 4294967295u) {
		return true; // If value at max probability, return always true
	}
	uint32_t randomChance = random(65535);
	return (value >> 16) >= randomChance;
}

int8_t ArpeggiatorBase::getRandomBipolarProbabilityAmount(uint32_t value) {
	if (value == 0) {
		return 0;
	}
	int32_t randValue = multiply_32x32_rshift32(q31_mult(sampleTriangleDistribution(), value >> 1), 255);
	if (randValue < -127) {
		return -127;
	}
	else if (randValue > 127) {
		return 127;
	}
	return randValue;
}
int8_t ArpeggiatorBase::getRandomWeighted2BitsAmount(uint32_t value) {
	if (value == 0) {
		return 0;
	}
	return std::abs(multiply_32x32_rshift32(q31_mult(sampleTriangleDistribution(), value >> 1), 5));
}

void ArpeggiatorBase::calculateRandomizerAmounts(ArpeggiatorSettings* settings) {
	if (settings->randomizerLock) {
		// Store generated randomized values in an array so we can repeat the sequence
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedNoteProbabilityParameterValue != settings->noteProbability) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedNoteProbabilityValues[i] = getRandomProbabilityResult(settings->noteProbability);
			}
			settings->lastLockedNoteProbabilityParameterValue = settings->noteProbability;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedBassProbabilityParameterValue != settings->bassProbability) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedBassProbabilityValues[i] = getRandomProbabilityResult(settings->bassProbability);
			}
			settings->lastLockedBassProbabilityParameterValue = settings->bassProbability;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedReverseProbabilityParameterValue != settings->reverseProbability) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedReverseProbabilityValues[i] = getRandomProbabilityResult(settings->reverseProbability);
			}
			settings->lastLockedReverseProbabilityParameterValue = settings->reverseProbability;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedChordProbabilityParameterValue != settings->chordProbability) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedChordProbabilityValues[i] = getRandomProbabilityResult(settings->chordProbability);
			}
			settings->lastLockedChordProbabilityParameterValue = settings->chordProbability;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedRatchetProbabilityParameterValue != settings->ratchetProbability) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedRatchetProbabilityValues[i] = getRandomProbabilityResult(settings->ratchetProbability);
			}
			settings->lastLockedRatchetProbabilityParameterValue = settings->ratchetProbability;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedSpreadVelocityParameterValue != settings->spreadVelocity) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedSpreadVelocityValues[i] =
				    getRandomBipolarProbabilityAmount(settings->spreadVelocity); // negative
			}
			settings->lastLockedSpreadVelocityParameterValue = settings->spreadVelocity;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedSpreadGateParameterValue != settings->spreadGate) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedSpreadGateValues[i] = getRandomBipolarProbabilityAmount(settings->spreadGate);
			}
			settings->lastLockedSpreadGateParameterValue = settings->spreadGate;
		}
		if (resetLockedRandomizerValuesNextTime
		    || settings->lastLockedSpreadOctaveParameterValue != settings->spreadOctave) {
			for (int i = 0; i < RANDOMIZER_LOCK_MAX_SAVED_VALUES; i++) {
				settings->lockedSpreadOctaveValues[i] = getRandomWeighted2BitsAmount(settings->spreadOctave);
			}
			settings->lastLockedSpreadOctaveParameterValue = settings->spreadOctave;
		}
		resetLockedRandomizerValuesNextTime = false;

		isPlayNoteForCurrentStep =
		    settings->lockedNoteProbabilityValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES]
		    != 0;
		isPlayBassForCurrentStep =
		    settings->lockedBassProbabilityValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES]
		    != 0;
		isPlayReverseForCurrentStep =
		    settings->lockedReverseProbabilityValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES]
		    != 0;
		isPlayChordForCurrentStep =
		    settings->lockedChordProbabilityValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES]
		    != 0;
		isPlayRatchetForCurrentStep =
		    settings->lockedRatchetProbabilityValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES]
		    != 0;
		spreadVelocityForCurrentStep =
		    settings->lockedSpreadVelocityValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES];
		spreadGateForCurrentStep =
		    settings->lockedSpreadGateValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES];
		spreadOctaveForCurrentStep =
		    settings->lockedSpreadOctaveValues[notesPlayedFromLockedRandomizer % RANDOMIZER_LOCK_MAX_SAVED_VALUES];
	}
	else {
		// Lively create new randomized values on the fly each time a note is played
		isPlayNoteForCurrentStep = getRandomProbabilityResult(settings->noteProbability);
		isPlayBassForCurrentStep = getRandomProbabilityResult(settings->bassProbability);
		isPlayReverseForCurrentStep = getRandomProbabilityResult(settings->reverseProbability);
		isPlayChordForCurrentStep = getRandomProbabilityResult(settings->chordProbability);
		isPlayRatchetForCurrentStep = getRandomProbabilityResult(settings->ratchetProbability);
		spreadVelocityForCurrentStep = getRandomBipolarProbabilityAmount(settings->spreadVelocity);
		spreadGateForCurrentStep = getRandomBipolarProbabilityAmount(settings->spreadGate);
		spreadOctaveForCurrentStep = getRandomWeighted2BitsAmount(settings->spreadOctave);
		// Rest locked values
		resetLockedRandomizerValuesNextTime = true;
	}
}

void ArpeggiatorBase::calculateNextNoteAndOrOctave(ArpeggiatorSettings* settings, uint8_t numActiveNotes) {
	if (stepRepeatIndex > 0) {
		// Don't got to next step if we still have to repeat the previous one
		//  stepRepeatIndex = 0 means we move to the next step
		//  stepRepeatIndex > 0 means we are in a "repeated" step
		return;
	}
	// If FULL-RANDOM (RANDOM for both Note and Octave)
	// we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->noteMode == ArpNoteMode::RANDOM && settings->octaveMode == ArpOctaveMode::RANDOM) {
		whichNoteCurrentlyOnPostArp = getRandom255() % numActiveNotes;
		currentOctave = getRandom255() % settings->numOctaves;
		// Must set all these variables here, even though RANDOM
		// doesn't use them, in case user changes arp mode.
		currentOctaveDirection = 1;
		randomNotesPlayedFromOctave = 0;
		currentDirection = 1;
	}

	// Or not FULL-RANDOM
	else {
		uint8_t numOctaves = settings->numOctaves;
		ArpOctaveMode octaveMode = settings->octaveMode;
		ArpNoteMode noteMode = settings->noteMode;

		// NOTE
		bool goesReverseOnlyThisStep = false;
		bool changeOctave = false;
		bool changingOctaveDirection = false;
		if (noteMode == ArpNoteMode::RANDOM) {
			// Note: Random
			whichNoteCurrentlyOnPostArp = getRandom255() % numActiveNotes;
			if (randomNotesPlayedFromOctave >= numActiveNotes) {
				changeOctave = true;
			}
		}
		else {
			// TODO do here the walk
			if (noteMode == ArpNoteMode::WALK1 || noteMode == ArpNoteMode::WALK2 || noteMode == ArpNoteMode::WALK3) {
				// Test if we need to invert direction

				// Probability of going forward is 50%
				uint8_t backwardsLimit = 64; // Normal: Walk2 (25% probability)
				uint8_t stayLimit = 128;     // Normal: Walk2 (25% probability)
				if (noteMode == ArpNoteMode::WALK3) {
					// Probability of going forward is 60%
					backwardsLimit = 51; // Fast: Walk3 (20% probability)
					stayLimit = 102;     // Fast: Walk3 (20% probability)
				}
				else if (noteMode == ArpNoteMode::WALK1) {
					// Probability of going forward is 40%
					backwardsLimit = 77; // Slow: Walk3 (30% probability)
					stayLimit = 154;     // Slow: Walk3 (30% probability)
				}

				uint8_t dice = getRandom255();
				if (dice < backwardsLimit) {
					// Go back one step
					goesReverseOnlyThisStep = true;
					noteMode = ArpNoteMode::DOWN; // Momentarily fool it to think it must go down
					if (octaveMode == ArpOctaveMode::UP) {
						octaveMode = ArpOctaveMode::DOWN;
					}
					else if (octaveMode == ArpOctaveMode::DOWN) {
						octaveMode = ArpOctaveMode::UP;
					}
					currentDirection = -currentDirection;             // Revert note direction just this step
					currentOctaveDirection = -currentOctaveDirection; // Revert octave direction just this step
				}
				else if (dice < stayLimit) {
					// Same step
					// Don't change anything
					return;
				}
				// dice >= 128 && dice < 256 (50% probability), just play normally
			}
			whichNoteCurrentlyOnPostArp += currentDirection;

			// If reached top of notes (so current direction must be up)
			if (whichNoteCurrentlyOnPostArp >= numActiveNotes) {
				changingOctaveDirection = (int32_t)currentOctave >= numOctaves - 1 && noteMode != ArpNoteMode::UP_DOWN
				                          && noteMode != ArpNoteMode::RANDOM && octaveMode == ArpOctaveMode::ALTERNATE;
				if (changingOctaveDirection) {
					// Now go down (without repeating)
					currentDirection = -1;
					whichNoteCurrentlyOnPostArp = numActiveNotes - 2;
				}
				else if (noteMode == ArpNoteMode::UP_DOWN) {
					// Now go down (repeating note)
					currentDirection = -1;
					whichNoteCurrentlyOnPostArp = numActiveNotes - 1;
				}
				else { // Up or AsPlayed
					// Start on next octave first note
					whichNoteCurrentlyOnPostArp = 0;
					changeOctave = true;
				}
			}
			// Or, if reached bottom of notes (so current direction must be down)
			else if (whichNoteCurrentlyOnPostArp < 0) {
				changingOctaveDirection = (int32_t)currentOctave <= 0 && noteMode != ArpNoteMode::UP_DOWN
				                          && noteMode != ArpNoteMode::RANDOM && octaveMode == ArpOctaveMode::ALTERNATE;
				if (changingOctaveDirection) {
					// Now go up
					currentDirection = 1;
					whichNoteCurrentlyOnPostArp = 1;
				}
				else if (noteMode == ArpNoteMode::UP_DOWN) { // UpDown
					// Start on next octave first note
					whichNoteCurrentlyOnPostArp = 0;
					currentDirection = 1;
					changeOctave = true;
				}
				else { // Down
					whichNoteCurrentlyOnPostArp = numActiveNotes - 1;
					changeOctave = true;
				}
			}
		}

		// OCTAVE
		if (changingOctaveDirection) {
			currentOctaveDirection = currentOctaveDirection == -1 ? 1 : -1;
		}
		if (changeOctave) {
			randomNotesPlayedFromOctave = 0; // reset this in any case
			if (numOctaves == 1) {
				currentOctave = 0;
				currentOctaveDirection = 1;
			}
			else if (octaveMode == ArpOctaveMode::RANDOM) {
				currentOctave = getRandom255() % numOctaves;
				currentOctaveDirection = 1;
			}
			else if (octaveMode == ArpOctaveMode::UP_DOWN || octaveMode == ArpOctaveMode::ALTERNATE) {
				currentOctave += currentOctaveDirection;
				if (currentOctave > numOctaves - 1) {
					// Now go down
					currentOctaveDirection = -1;
					if (octaveMode == ArpOctaveMode::ALTERNATE) {
						currentOctave = numOctaves - 2;
					}
					else {
						currentOctave = numOctaves - 1;
					}
				}
				else if (currentOctave < 0) {
					// Now go up
					currentOctaveDirection = 1;
					if (octaveMode == ArpOctaveMode::ALTERNATE) {
						currentOctave = 1;
					}
					else {
						currentOctave = 0;
					}
				}
			}
			else {
				// Have to reset this, in case the user changed the setting.
				currentOctaveDirection = (octaveMode == ArpOctaveMode::DOWN) ? -1 : 1;
				currentOctave += currentOctaveDirection;
				if (currentOctave >= numOctaves) {
					currentOctave = 0;
				}
				else if (currentOctave < 0) {
					currentOctave = numOctaves - 1;
				}
			}
		}
		if (goesReverseOnlyThisStep) {
			// Restore normal directions for next step calculation
			currentDirection = -currentDirection;
			currentOctaveDirection = -currentOctaveDirection;
		}
	}
}

// Set initial values for note and octave
void ArpeggiatorBase::setInitialNoteAndOctave(ArpeggiatorSettings* settings, uint8_t numActiveNotes) {
	// NOTE
	if (settings->noteMode == ArpNoteMode::RANDOM) {
		// Note: Random
		whichNoteCurrentlyOnPostArp = getRandom255() % numActiveNotes;
		currentDirection = 1;
	}
	else if (settings->noteMode == ArpNoteMode::DOWN) {
		// Note: Down
		whichNoteCurrentlyOnPostArp = numActiveNotes - 1;
		currentDirection = -1;
	}
	else {
		// Note: Up or Up&Down
		whichNoteCurrentlyOnPostArp = 0;
		currentDirection = 1;
	}

	// OCTAVE
	if (settings->octaveMode == ArpOctaveMode::RANDOM) {
		// Octave: Random
		currentOctave = getRandom255() % settings->numOctaves;
		currentOctaveDirection = 1;
	}
	else if (settings->octaveMode == ArpOctaveMode::DOWN
	         || (settings->octaveMode == ArpOctaveMode::ALTERNATE && settings->noteMode == ArpNoteMode::DOWN)) {
		// Octave: Down or Alternate (with note down)
		currentOctave = settings->numOctaves - 1;
		currentOctaveDirection = -1;
	}
	else {
		// Octave: Up or Up&Down or Alternate (with note up)
		currentOctave = 0;
		currentOctaveDirection = 1;
	}
}

void Arpeggiator::rearrangePatterntArpNotes(ArpeggiatorSettings* settings) {
	// Wipe the array
	notesByPattern.empty();
	// Add again the note codes in the order dictated by the pattern
	Error error;
	uint32_t notesByPatternIndex;
	int32_t numNotes = notes.getNumElements();
	for (uint32_t i = 0; i < numNotes; i++) {
		notesByPatternIndex = std::min((uint32_t)settings->notePattern[std::min(i, PATTERN_MAX_BUFFER_SIZE - 1)], i);
		// insert at the position in the array indicated by the pattern
		error = notesByPattern.insertAtIndex(notesByPatternIndex);
		if (error != Error::NONE) {
			continue;
		}
		// Save arpNote
		ArpNote* arpNote = (ArpNote*)notes.getElementAddress(i);
		ArpJustNoteCode* arpByPatternNote = (ArpJustNoteCode*)notesByPattern.getElementAddress(notesByPatternIndex);
		arpByPatternNote->noteCode = arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];
	}
}

void Arpeggiator::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) {
	// Get params
	uint32_t maxSequenceLength = computeCurrentValueForUnsignedMenuItem(settings->sequenceLength);
	uint32_t rhythm = computeCurrentValueForUnsignedMenuItem(settings->rhythm);

	// Probabilities
	bool shouldCarryOnRhythmNote = evaluateRhythm(rhythm, isRatchet);
	if (shouldCarryOnRhythmNote && !isRatchet) {
		// Calculate randomizer amounts only for non-silent and non-ratchet notes
		calculateRandomizerAmounts(settings);
	}
	bool shouldPlayNote = evaluateNoteProbability(isRatchet);
	bool shouldPlayBassNote = evaluateBassProbability(isRatchet);
	bool shouldPlayReverseNote = evaluateReverseProbability(isRatchet);
	bool shouldPlayChordNote = evaluateChordProbability(isRatchet);

	// Execute all the step calculations
	executeArpStep(settings, (uint8_t)notes.getNumElements(), isRatchet, maxSequenceLength, rhythm,
	               shouldCarryOnRhythmNote, shouldPlayNote, shouldPlayBassNote, shouldPlayReverseNote,
	               shouldPlayChordNote);

	// Clamp the index to real range
	whichNoteCurrentlyOnPostArp = std::clamp<int16_t>(whichNoteCurrentlyOnPostArp, 0, notes.getNumElements() - 1);

	ArpNote* arpNote;
	if (shouldPlayBassNote) {
		arpNote = (ArpNote*)notes.getElementAddress(0);
	}
	else if (settings->noteMode == ArpNoteMode::AS_PLAYED) {
		ArpJustNoteCode* arpAsPlayedNote =
		    (ArpJustNoteCode*)notesAsPlayed.getElementAddress(whichNoteCurrentlyOnPostArp);
		int32_t notesKey = notes.search(arpAsPlayedNote->noteCode, GREATER_OR_EQUAL);
		if (notesKey < notes.getNumElements()) [[likely]] {
			arpNote = (ArpNote*)notes.getElementAddress(notesKey);
		}
		else {
			// Very unlikely, but just in case
			arpNote = (ArpNote*)notes.getElementAddress(0);
		}
	}
	else if (settings->noteMode == ArpNoteMode::PATTERN) {
		ArpJustNoteCode* arpByPatternNote =
		    (ArpJustNoteCode*)notesByPattern.getElementAddress(whichNoteCurrentlyOnPostArp);
		int32_t notesKey = notes.search(arpByPatternNote->noteCode, GREATER_OR_EQUAL);
		if (notesKey < notes.getNumElements()) [[likely]] {
			arpNote = (ArpNote*)notes.getElementAddress(notesKey);
		}
		else {
			// Very unlikely, but just in case
			arpNote = (ArpNote*)notes.getElementAddress(0);
		}
	}
	else {
		arpNote = (ArpNote*)notes.getElementAddress(whichNoteCurrentlyOnPostArp);
	}

	if (shouldCarryOnRhythmNote && shouldPlayNote) {
		// Set Gate as active
		gateCurrentlyActive = true;
		if (!isRatchet) {
			// Reset gate position (only for normal notes)
			gatePos = 0;
		}

		uint8_t velocity = arpNote->baseVelocity;

		if (settings->mpeVelocity != ArpMpeModSource::OFF) {
			// Check if we need to update velocity with some MPE value
			switch (settings->mpeVelocity) {
			case ArpMpeModSource::AFTERTOUCH:
				velocity = arpNote->mpeValues[util::to_underlying(Expression::Z_PRESSURE)] >> 8;
				break;
			case ArpMpeModSource::MPE_Y:
				velocity = arpNote->mpeValues[util::to_underlying(Expression::Y_SLIDE_TIMBRE)] >> 8;
				break;
			// explicit fallthrough case
			case ArpMpeModSource::OFF:;
			}
			// Fix base velocity if it's outside of MPE-controlled bounds
			if (velocity < MIN_MPE_MODULATED_VELOCITY) {
				velocity = MIN_MPE_MODULATED_VELOCITY;
			}
		}
		arpNote->baseVelocity = velocity;
		// Now apply velocity spread to the base velocity
		velocity = calculateSpreadVelocity(velocity, spreadVelocityForCurrentStep);
		arpNote->velocity = velocity;
		// Get current sequence note
		int16_t note;
		if (shouldPlayBassNote) {
			note = arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];
		}
		else {
			int16_t diff = (int16_t)currentOctave * 12;
			if (spreadOctaveForCurrentStep != 0) {
				// Now apply octave spread to the base note
				diff = diff + spreadOctaveForCurrentStep * 12;
			}
			note = arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] + diff;
		}
		// And fix it if out of bounds
		if (note < 0) {
			note = 0;
		}
		else if (note > 127) {
			note = 127;
		}

		// Wipe noteOn codes
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			noteCodeCurrentlyOnPostArp[n] = ARP_NOTE_NONE;
			arpNote->noteCodeOnPostArp[n] = ARP_NOTE_NONE;
		}

		// Set the note(s) to be played
		noteCodeCurrentlyOnPostArp[0] = note; // This is the main note, whether we play chord or not

		// Now get additional notes to be played
		MusicalKey musicalKey = currentSong->key;
		int8_t degree = musicalKey.degreeOf(note);
		if (shouldPlayChordNote && degree >= 0 && musicalKey.modeNotes.count() >= 5) {
			// Play chord!
			// Limitation: we will only try to play chords for notes in the scale, and if scale has at least 5 notes
			int8_t baseOffset = musicalKey.modeNotes[degree % musicalKey.modeNotes.count()];
			int8_t numAdditionalNotesInChord =
			    std::min((int8_t)3, getRandomWeighted2BitsAmount(settings->chordPolyphony));
			int8_t degreeOffsets[3] = {0, 0, 0};
			if (numAdditionalNotesInChord > 0) {
				switch (numAdditionalNotesInChord) {
				case 1:
					degreeOffsets[0] = 4;
					break;
				case 2:
					degreeOffsets[0] = 2;
					degreeOffsets[1] = 4;
					break;
				case 3:
					degreeOffsets[0] = 2;
					degreeOffsets[1] = 4;
					degreeOffsets[2] = 6;
					break;
				default:
					break;
				}
				for (int32_t n = 1; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
					if (n <= numAdditionalNotesInChord) {
						// Pick the note to be added
						int8_t targetOffset =
						    musicalKey.modeNotes[(degree + degreeOffsets[n - 1]) % musicalKey.modeNotes.count()];
						if (targetOffset <= baseOffset) {
							// If the note is lower than the base note, we need to add an octave
							targetOffset += 12;
						}
						noteCodeCurrentlyOnPostArp[n] = note + targetOffset - baseOffset;
					}
				}
			}
		}

		// Copy notes to the arp return instruction object
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			arpNote->noteCodeOnPostArp[n] = noteCodeCurrentlyOnPostArp[n];
		}
		instruction->invertReversed = shouldPlayReverseNote;
		instruction->arpNoteOn = arpNote;
	}
}

bool Arpeggiator::hasAnyInputNotesActive() {
	return notes.getNumElements();
}

bool ArpeggiatorForDrum::hasAnyInputNotesActive() {
	return arpNote.velocity;
}

// Check arpeggiator is on before you call this.
// May switch notes on and/or off.
void ArpeggiatorBase::render(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, int32_t numSamples,
                             uint32_t gateThreshold, uint32_t phaseIncrement) {
	if (settings->mode == ArpMode::OFF || !hasAnyInputNotesActive()) {
		return;
	}

	uint32_t maxGate = 1 << 24;

	uint32_t gateThresholdSmall = gateThreshold >> 8;

	if (spreadGateForCurrentStep != 0) {
		// Apply spread to gate threshold
		int32_t signedGateThreshold = (int32_t)gateThresholdSmall;
		int32_t diff = 0;
		if (spreadGateForCurrentStep < 0) {
			// Reducing gate
			diff = -(multiply_32x32_rshift32((-spreadGateForCurrentStep) << 24, signedGateThreshold) << 1);
		}
		else {
			// Increasing gate
			diff = (multiply_32x32_rshift32(spreadGateForCurrentStep << 24, maxGate - signedGateThreshold) << 1);
		}
		signedGateThreshold = signedGateThreshold + diff;
		// And fix it if out of bounds
		if (signedGateThreshold < 0) {
			signedGateThreshold = 0;
		}
		else if (signedGateThreshold >= maxGate) {
			signedGateThreshold = maxGate - 1;
		}
		// Convert back to unsigned
		gateThresholdSmall = (uint32_t)signedGateThreshold;
	}

	if (isRatcheting) {
		// shorten gate in case we are ratcheting (with the calculated number of ratchet notes)
		gateThresholdSmall = gateThresholdSmall >> ratchetNotesMultiplier;
	}
	uint32_t maxGateForRatchet = maxGate >> ratchetNotesMultiplier;

	bool syncedNow = (settings->syncLevel && (playbackHandler.isEitherClockActive()));

	// If gatePos is far enough along that we at least want to switch off any note...
	if (gatePos >= ratchetNotesIndex * maxGateForRatchet + gateThresholdSmall) {
		switchAnyNoteOff(instruction);

		// If it's ratcheting and time for the next ratchet event
		if (isRatcheting && ratchetNotesIndex < ratchetNotesCount - 1
		    && gatePos >= (ratchetNotesIndex + 1) * maxGateForRatchet) {
			// Switch on next note in the ratchet
			switchNoteOn(settings, instruction, true);
		}
		// And maybe (if not syncing) the gatePos is also far enough along that we also want to switch a normal note on?
		else if (!syncedNow && gatePos >= maxGate) {
			switchNoteOn(settings, instruction, false);
		}
	}
	if (!syncedNow) {
		gatePos &= (maxGate - 1);
	}

	gatePos += (phaseIncrement >> 8) * numSamples;
}

// Returns num ticks til we next want to come back here.
// May switch notes on and/or off.
int32_t ArpeggiatorBase::doTickForward(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction,
                                       uint32_t clipCurrentPos, bool currentlyPlayingReversed) {
	if (clipCurrentPos == 0) {
		notesPlayedFromLockedRandomizer = 0;
	}
	// Make sure we actually intended to sync
	if (settings->mode == ArpMode::OFF || (settings->syncLevel == 0u)) {
		return 2147483647;
	}

	uint32_t ticksPerPeriod = 3 << (9 - settings->syncLevel);
	if (settings->syncType == SYNC_TYPE_EVEN) {} // Do nothing
	else if (settings->syncType == SYNC_TYPE_TRIPLET) {
		ticksPerPeriod = ticksPerPeriod * 2 / 3;
	}
	else if (settings->syncType == SYNC_TYPE_DOTTED) {
		ticksPerPeriod = ticksPerPeriod * 3 / 2;
	}

	int32_t howFarIntoPeriod = clipCurrentPos % ticksPerPeriod;

	if (!howFarIntoPeriod) {
		if (hasAnyInputNotesActive()) {
			switchAnyNoteOff(instruction);
			switchNoteOn(settings, instruction, false);

			instruction->sampleSyncLengthOn = ticksPerPeriod; // Overwrite this
		}
		howFarIntoPeriod = ticksPerPeriod;
	}
	else {
		if (!currentlyPlayingReversed) {
			howFarIntoPeriod = ticksPerPeriod - howFarIntoPeriod;
		}
	}
	return howFarIntoPeriod; // Normally we will have modified this variable above, and it no longer represents what its
	                         // name says.
}

uint32_t ArpeggiatorSettings::getPhaseIncrement(int32_t arpRate) {
	uint32_t phaseIncrement;
	if (syncLevel == 0) {
		phaseIncrement = arpRate >> 5;
	}
	else {
		int32_t rightShiftAmount = 9 - syncLevel; // Will be max 0
		phaseIncrement =
		    playbackHandler
		        .getTimePerInternalTickInverse(); // multiply_32x32_rshift32(playbackHandler.getTimePerInternalTickInverse(),
		                                          // arpRate);
		phaseIncrement >>= rightShiftAmount;

		if (syncType == SYNC_TYPE_TRIPLET) {
			phaseIncrement = phaseIncrement * 3 / 2;
		}
		else if (syncType == SYNC_TYPE_DOTTED) {
			phaseIncrement = phaseIncrement * 2 / 3;
		}
	}
	return phaseIncrement;
}

void ArpeggiatorSettings::cloneFrom(ArpeggiatorSettings const* other) {
	// Static params
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
	// Automatable params
	rate = other->rate;
	gate = other->gate;
	rhythm = other->rhythm;
	sequenceLength = other->sequenceLength;
	chordPolyphony = other->chordPolyphony;
	ratchetAmount = other->ratchetAmount;
	noteProbability = other->noteProbability;
	bassProbability = other->bassProbability;
	reverseProbability = other->reverseProbability;
	chordProbability = other->chordProbability;
	ratchetProbability = other->ratchetProbability;
	spreadVelocity = other->spreadVelocity;
	spreadGate = other->spreadGate;
	spreadOctave = other->spreadOctave;
}

bool ArpeggiatorSettings::readCommonTagsFromFile(Deserializer& reader, char const* tagName,
                                                 Song* songToConvertSyncLevel) {
	if (!strcmp(tagName, "numOctaves")) {
		numOctaves = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "stepRepeat")) {
		numStepRepeats = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "randomizerLock")) {
		randomizerLock = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lastLockedNoteProb")) {
		lastLockedNoteProbabilityParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockedNoteProbArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedNoteProbabilityValues.data(),
		                                                 lockedNoteProbabilityValues.size());
	}
	else if (!strcmp(tagName, "lastLockedBassProb")) {
		lastLockedBassProbabilityParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockedBassProbArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedBassProbabilityValues.data(),
		                                                 lockedBassProbabilityValues.size());
	}
	else if (!strcmp(tagName, "lastLockedReverseProb")) {
		lastLockedReverseProbabilityParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockeReverseProbArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedReverseProbabilityValues.data(),
		                                                 lockedReverseProbabilityValues.size());
	}
	else if (!strcmp(tagName, "lastLockedChordProb")) {
		lastLockedChordProbabilityParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockeChordProbArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedChordProbabilityValues.data(),
		                                                 lockedChordProbabilityValues.size());
	}
	else if (!strcmp(tagName, "lastLockedRatchetProb")) {
		lastLockedRatchetProbabilityParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockeRatchetProbArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedRatchetProbabilityValues.data(),
		                                                 lockedRatchetProbabilityValues.size());
	}
	else if (!strcmp(tagName, "lastLockedVelocitySpread")) {
		lastLockedSpreadVelocityParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockedVelocitySpreadArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedSpreadVelocityValues.data(),
		                                                 lockedSpreadVelocityValues.size());
	}
	else if (!strcmp(tagName, "lastLockedGateSpread")) {
		lastLockedSpreadGateParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockedGateSpreadArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedSpreadGateValues.data(),
		                                                 lockedSpreadGateValues.size());
	}
	else if (!strcmp(tagName, "lastLockedOctaveSpread")) {
		lastLockedSpreadOctaveParameterValue = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "lockedOctaveSpreadArray")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)lockedSpreadOctaveValues.data(),
		                                                 lockedSpreadOctaveValues.size());
	}
	else if (!strcmp(tagName, "notePattern")) {
		int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)notePattern.data(), notePattern.size());
	}
	else if (!strcmp(tagName, "syncLevel")) {
		if (songToConvertSyncLevel) {
			syncLevel = (SyncLevel)songToConvertSyncLevel->convertSyncLevelFromFileValueToInternalValue(
			    reader.readTagOrAttributeValueInt());
		}
		else {
			syncLevel = (SyncLevel)reader.readTagOrAttributeValueInt();
		}
	}
	else if (!strcmp(tagName, "syncType")) {
		syncType = (SyncType)reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "mode")) {
		if (song_firmware_version < FirmwareVersion::community({1, 1, 0})) {
			// Import the old "mode" into the new splitted params "arpMode", "noteMode", and "octaveMode
			// but only if the new params are not already read and set,
			// that is, if we detect they have a value other than default
			OldArpMode oldMode = stringToOldArpMode(reader.readTagOrAttributeValue());
			if (mode == ArpMode::OFF && noteMode == ArpNoteMode::UP && octaveMode == ArpOctaveMode::UP) {
				mode = oldModeToArpMode(oldMode);
				noteMode = oldModeToArpNoteMode(oldMode);
				octaveMode = oldModeToArpOctaveMode(oldMode);
				updatePresetFromCurrentSettings();
			}
		}
	}
	else if (!strcmp(tagName, "arpMode")) {
		mode = stringToArpMode(reader.readTagOrAttributeValue());
		updatePresetFromCurrentSettings();
	}
	else if (!strcmp(tagName, "octaveMode")) {
		octaveMode = stringToArpOctaveMode(reader.readTagOrAttributeValue());
		updatePresetFromCurrentSettings();
	}
	else if (!strcmp(tagName, "chordType")) {
		uint8_t value = (uint8_t)reader.readTagOrAttributeValueInt();
		if (value >= 0 && value < MAX_CHORD_TYPES) {
			chordTypeIndex = value;
		}
	}
	else if (!strcmp(tagName, "noteMode")) {
		noteMode = stringToArpNoteMode(reader.readTagOrAttributeValue());
		updatePresetFromCurrentSettings();
	}
	else if (!strcmp(tagName, "mpeVelocity")) {
		mpeVelocity = stringToArpMpeModSource(reader.readTagOrAttributeValue());
	}
	else {
		return false;
	}

	reader.exitTag(tagName);
	return true;
}

bool ArpeggiatorSettings::readNonAudioTagsFromFile(Deserializer& reader, char const* tagName) {
	if (!strcmp(tagName, "rate")) {
		rate = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "gate")) {
		gate = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "noteProbability")) {
		noteProbability = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "bassProbability")) {
		bassProbability = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "reverseProbability")) {
		reverseProbability = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "chordProbability")) {
		chordProbability = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "ratchetProbability")) {
		ratchetProbability = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "ratchetAmount")) {
		ratchetAmount = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "sequenceLength")) {
		sequenceLength = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "chordPolyphony")) {
		chordPolyphony = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "rhythm")) {
		rhythm = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "spreadVelocity")) {
		spreadVelocity = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "spreadGate")) {
		spreadGate = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "spreadOctave")) {
		spreadOctave = reader.readTagOrAttributeValueInt();
	}
	else {
		return false;
	}

	reader.exitTag(tagName);
	return true;
}

void ArpeggiatorSettings::writeCommonParamsToFile(Serializer& writer, Song* songToConvertSyncLevel) {
	writer.writeAttribute("mode", (char*)arpModeToString(mode));
	if (songToConvertSyncLevel) {
		writer.writeAbsoluteSyncLevelToFile(songToConvertSyncLevel, "syncLevel", syncLevel, true);
	}
	else {
		writer.writeAttribute("syncLevel", syncLevel, true);
	}
	writer.writeAttribute("numOctaves", numOctaves);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	if (songToConvertSyncLevel) {
		writer.writeSyncTypeToFile(songToConvertSyncLevel, "syncType", syncType, true);
	}
	else {
		writer.writeAttribute("syncType", syncType);
	}
	writer.writeAttribute("arpMode", (char*)arpModeToString(mode));
	writer.writeAttribute("chordType", chordTypeIndex);
	writer.writeAttribute("noteMode", (char*)arpNoteModeToString(noteMode));
	writer.writeAttribute("octaveMode", (char*)arpOctaveModeToString(octaveMode));
	writer.writeAttribute("mpeVelocity", (char*)arpMpeModSourceToString(mpeVelocity));
	writer.writeAttribute("stemRepeat", numStepRepeats);
	writer.writeAttribute("randomizerLock", randomizerLock);

	// Note probability
	writer.writeAttribute("lastLockedNoteProb", lastLockedNoteProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedNoteProbArray", (uint8_t*)lockedNoteProbabilityValues.data(),
	                              lockedNoteProbabilityValues.size());
	// Bass probability
	writer.writeAttribute("lastLockedBassProb", lastLockedBassProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedBassProbArray", (uint8_t*)lockedBassProbabilityValues.data(),
	                              lockedBassProbabilityValues.size());
	// Reverse probability
	writer.writeAttribute("lastLockedReverseProb", lastLockedReverseProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedReverseProbArray", (uint8_t*)lockedReverseProbabilityValues.data(),
	                              lockedReverseProbabilityValues.size());
	// Chord probability
	writer.writeAttribute("lastLockedChordProb", lastLockedChordProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedChordProbArray", (uint8_t*)lockedChordProbabilityValues.data(),
	                              lockedChordProbabilityValues.size());
	// Ratchet probability
	writer.writeAttribute("lastLockedRatchetProb", lastLockedRatchetProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedRatchetProbArray", (uint8_t*)lockedRatchetProbabilityValues.data(),
	                              lockedRatchetProbabilityValues.size());
	// Spread velocity
	writer.writeAttribute("lastLockedVelocitySpread", lastLockedSpreadVelocityParameterValue);
	writer.writeAttributeHexBytes("lockedVelocitySpreadArray", (uint8_t*)lockedSpreadVelocityValues.data(),
	                              lockedSpreadVelocityValues.size());
	// Spread gate
	writer.writeAttribute("lastLockedGateSpread", lastLockedSpreadGateParameterValue);
	writer.writeAttributeHexBytes("lockedGateSpreadArray", (uint8_t*)lockedSpreadGateValues.data(),
	                              lockedSpreadGateValues.size());
	// Spread octave
	writer.writeAttribute("lastLockedOctaveSpread", lastLockedSpreadOctaveParameterValue);
	writer.writeAttributeHexBytes("lockedOctaveSpreadArray", (uint8_t*)lockedSpreadOctaveValues.data(),
	                              lockedSpreadOctaveValues.size());
	// Note pattern
	writer.writeAttributeHexBytes("notePattern", (uint8_t*)notePattern.data(), notePattern.size());
}

void ArpeggiatorSettings::writeNonAudioParamsToFile(Serializer& writer) {
	writer.writeAttribute("gate", gate);
	writer.writeAttribute("rate", rate);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent
	// tag)
	writer.writeAttribute("noteProbability", noteProbability);
	writer.writeAttribute("bassProbability", bassProbability);
	writer.writeAttribute("reverseProbability", reverseProbability);
	writer.writeAttribute("chordProbability", chordProbability);
	writer.writeAttribute("ratchetProbability", ratchetProbability);
	writer.writeAttribute("ratchetAmount", ratchetAmount);
	writer.writeAttribute("sequenceLength", sequenceLength);
	writer.writeAttribute("chordPolyphony", chordPolyphony);
	writer.writeAttribute("rhythm", rhythm);
	writer.writeAttribute("spreadVelocity", spreadVelocity);
	writer.writeAttribute("spreadGate", spreadGate);
	writer.writeAttribute("spreadOctave", spreadOctave);
}

void ArpeggiatorSettings::generateNewNotePattern() {
	for (int32_t i = 0; i < PATTERN_MAX_BUFFER_SIZE; i++) {
		notePattern[i] = getRandom255() % (i + 1);
	}
}

void ArpeggiatorSettings::updatePresetFromCurrentSettings() {
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
	else if (octaveMode == ArpOctaveMode::ALTERNATE && noteMode == ArpNoteMode::WALK2) {
		preset = ArpPreset::WALK;
	}
	else {
		preset = ArpPreset::CUSTOM;
	}
}

void ArpeggiatorSettings::updateParamsFromUnpatchedParamSet(UnpatchedParamSet* unpatchedParams) {
	rhythm = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_RHYTHM) + 2147483648;
	sequenceLength = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_SEQUENCE_LENGTH) + 2147483648;
	chordPolyphony = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_CHORD_POLYPHONY) + 2147483648;
	ratchetAmount = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_RATCHET_AMOUNT) + 2147483648;
	noteProbability = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_NOTE_PROBABILITY) + 2147483648;
	bassProbability = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_BASS_PROBABILITY) + 2147483648;
	reverseProbability = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_REVERSE_PROBABILITY) + 2147483648;
	chordProbability = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_CHORD_PROBABILITY) + 2147483648;
	ratchetProbability = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_RATCHET_PROBABILITY) + 2147483648;
	spreadVelocity = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_SPREAD_VELOCITY) + 2147483648;
	spreadGate = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_SPREAD_GATE) + 2147483648;
	spreadOctave = (uint32_t)unpatchedParams->getValue(params::UNPATCHED_ARP_SPREAD_OCTAVE) + 2147483648;
}

void ArpeggiatorSettings::updateSettingsFromCurrentPreset() {
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
	else if (preset == ArpPreset::WALK) {
		mode = ArpMode::ARP;
		octaveMode = ArpOctaveMode::ALTERNATE;
		noteMode = ArpNoteMode::WALK2;
	}
	else if (preset == ArpPreset::CUSTOM) {
		mode = ArpMode::ARP;
		// Although CUSTOM has octaveMode and noteMode freely setable, when we select CUSTOM from the preset menu
		// shortcut, we can provide here some default starting settings that user can change later with the menus.
		octaveMode = ArpOctaveMode::UP;
		noteMode = ArpNoteMode::UP;
	}
}
