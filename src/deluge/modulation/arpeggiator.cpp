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
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"

#define MIN_MPE_MODULATED_VELOCITY 10

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
}

ArpeggiatorForDrum::ArpeggiatorForDrum() {
	ratchetingIsAvailable = false;
	arpNote.velocity = 0;
}

Arpeggiator::Arpeggiator() : notes(sizeof(ArpNote), 16, 0, 8, 8), notesAsPlayed(sizeof(ArpNote), 8, 8) {
	notes.emptyingShouldFreeMemory = false;
	notesAsPlayed.emptyingShouldFreeMemory = false;
}

// Surely this shouldn't be quite necessary?
void ArpeggiatorForDrum::reset() {
	arpNote.velocity = 0;
}

void ArpeggiatorBase::resetRatchet() {
	isRatcheting = false;
	ratchetNotesMultiplier = 0;
	ratchetNotesCount = 0;
	ratchetNotesIndex = 0;
}

void ArpeggiatorBase::resetRhythm() {
	notesPlayedFromRhythm = 0;
	lastNormalNotePlayedFromRhythm = 0;
}

void Arpeggiator::reset() {
	notes.empty();
	notesAsPlayed.empty();
}

void ArpeggiatorForDrum::noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity,
                                ArpReturnInstruction* instruction, int32_t fromMIDIChannel, int16_t const* mpeValues) {
	lastVelocity = velocity;

	bool wasActiveBefore = arpNote.velocity;

	arpNote.inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = noteCode;
	arpNote.inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = fromMIDIChannel;
	arpNote.velocity = velocity; // Means note is on.
	// MIDIInstrument might set this later, but it needs to be MIDI_CHANNEL_NONE until then so it doesn't get included
	// in the survey that will happen of existing output member channels.
	arpNote.outputMemberChannel = MIDI_CHANNEL_NONE;

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
		instruction->noteCodeOnPostArp = noteCode;
		instruction->arpNoteOn = &arpNote;
	}
}

void ArpeggiatorForDrum::noteOff(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) {

	// If no arpeggiation...
	if ((settings == nullptr) || settings->mode == ArpMode::OFF) {
		instruction->noteCodeOffPostArp = kNoteForDrum;
		instruction->outputMIDIChannelOff = arpNote.outputMemberChannel;
	}

	// Or if yes arpeggiation...
	else {
		if (gateCurrentlyActive) {
			instruction->noteCodeOffPostArp = noteCodeCurrentlyOnPostArp;
			instruction->outputMIDIChannelOff = outputMIDIChannelForNoteCurrentlyOnPostArp;
		}
	}

	arpNote.velocity = 0; // Means note is off
}

// May return the instruction for a note-on, or no instruction. The noteCode instructed might be some octaves up from
// that provided here.
void Arpeggiator::noteOn(ArpeggiatorSettings* settings, int32_t noteCode, int32_t velocity,
                         ArpReturnInstruction* instruction, int32_t fromMIDIChannel, int16_t const* mpeValues) {
	lastVelocity = velocity;

	bool noteExists = false;

	ArpNote* arpNote;
	ArpNote* arpNoteAsPlayed;

	int32_t notesKey = notes.search(noteCode, GREATER_OR_EQUAL);
	if (notesKey < notes.getNumElements()) [[unlikely]] {
		arpNote = (ArpNote*)notes.getElementAddress(notesKey);
		if (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCode) {
			noteExists = true;
		}
	}
	int32_t notesAsPlayedIndex = -1;
	for (int32_t i = 0; i < notesAsPlayed.getNumElements(); i++) {
		ArpNote* theArpNote = (ArpNote*)notesAsPlayed.getElementAddress(i);
		if (theArpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCode) {
			arpNoteAsPlayed = theArpNote;
			notesAsPlayedIndex = i;
			noteExists = true;
			break;
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
		arpNote->velocity = velocity;
		// MIDIInstrument might set this, but it needs to be MIDI_CHANNEL_NONE until then so it
		// doesn't get included in the survey that will happen of existing output member
		// channels.
		arpNote->outputMemberChannel = MIDI_CHANNEL_NONE;
		// Update expression values
		for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
			arpNote->mpeValues[m] = mpeValues[m];
		}

		// "AS PLAYED" NOTES

		// Insert it in notesAsPlayed array
		notesAsPlayedIndex = notesAsPlayed.getNumElements();
		error = notesAsPlayed.insertAtIndex(notesAsPlayedIndex); // always insert at the end or the array
		if (error != Error::NONE) {
			return;
		}
		// Save arpNote
		arpNoteAsPlayed = (ArpNote*)notesAsPlayed.getElementAddress(notesAsPlayedIndex);
		arpNoteAsPlayed->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = noteCode;
		arpNoteAsPlayed->velocity = velocity;
		// MIDIInstrument might set this, but it needs to be MIDI_CHANNEL_NONE until then so it
		// doesn't get included in the survey that will happen of existing output member
		// channels.
		arpNoteAsPlayed->outputMemberChannel = MIDI_CHANNEL_NONE;
		// Update expression values
		for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
			arpNoteAsPlayed->mpeValues[m] = mpeValues[m];
		}
	}

noteInserted:
	// This is here so that "stealing" a note being edited can then replace its MPE data during
	// editing. Kind of a hacky solution, but it works for now.
	arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = fromMIDIChannel;
	arpNoteAsPlayed->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = fromMIDIChannel;

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
		instruction->noteCodeOnPostArp = noteCode;
		instruction->arpNoteOn = arpNote;
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
				instruction->noteCodeOffPostArp = noteCodePreArp;
				instruction->outputMIDIChannelOff = arpNote->outputMemberChannel;
			}

			// Or if yes arpeggiation, we'll only stop right now if that was the last note to switch off. Otherwise,
			// it'll turn off soon with the arpeggiation.
			else {
				if (notes.getNumElements() == 1) {
					if (whichNoteCurrentlyOnPostArp == notesKey && gateCurrentlyActive) {
						instruction->noteCodeOffPostArp = noteCodeCurrentlyOnPostArp;
						instruction->outputMIDIChannelOff = outputMIDIChannelForNoteCurrentlyOnPostArp;
					}
				}
			}

			notes.deleteAtIndex(notesKey);
			// We must also search and delete from notesAsPlayed
			int numNotes = notesAsPlayed.getNumElements();
			for (int32_t i = 0; i < numNotes; i++) {
				ArpNote* arpNote = (ArpNote*)notesAsPlayed.getElementAddress(i);
				if (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCodePreArp) {
					notesAsPlayed.deleteAtIndex(i);
					if (arpOff && i == numNotes - 1 && i > 0) {
						// if we're not arpeggiating then pass the second last note back - cv instruments will snap back
						// to it (like playing a normal mono synth)
						ArpNote* newArpNote = (ArpNote*)notesAsPlayed.getElementAddress(i - 1);
						instruction->noteCodeOnPostArp =
						    newArpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];
						instruction->arpNoteOn = newArpNote;
					}
					break;
				}
			}

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
		resetRatchet();
		resetRhythm();
		playedFirstArpeggiatedNoteYet = false;
	}
}

void ArpeggiatorBase::switchAnyNoteOff(ArpReturnInstruction* instruction) {
	if (gateCurrentlyActive) {
		instruction->noteCodeOffPostArp = noteCodeCurrentlyOnPostArp;
		instruction->outputMIDIChannelOff = outputMIDIChannelForNoteCurrentlyOnPostArp;
		gateCurrentlyActive = false;
	}
}

// Sets up the ratchet state if the probability is met
void ArpeggiatorBase::maybeSetupNewRatchet(ArpeggiatorSettings* settings) {
	int32_t randomChance = random(65535);
	isRatcheting = ratchetProbability >= randomChance
	               && ratchetAmount > 0
	               // For the highest sync we can't divide the time any more, so not possible to ratchet
	               && !(settings->syncType == SYNC_TYPE_EVEN && settings->syncLevel == SyncLevel::SYNC_LEVEL_256TH);
	if (isRatcheting) {
		ratchetNotesMultiplier = random(65535) % (ratchetAmount + 1);
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

// Returns if the note arpeggiator should advance to next note or not
bool ArpeggiatorBase::evaluateRhythm(bool isRatchet) {
	// If it is a rachet, use the last normal note played index, but it it is not a ratchet, play the new rhythm index
	int32_t rhythmPatternIndex = isRatchet ? lastNormalNotePlayedFromRhythm : notesPlayedFromRhythm;
	int32_t numberOfRhythmSteps = arpRhythmPatterns[rhythm].length;
	rhythmPatternIndex = rhythmPatternIndex % numberOfRhythmSteps; // normalize the index just in case
	return arpRhythmPatterns[rhythm].steps[rhythmPatternIndex];
}

void ArpeggiatorBase::carryOnOctaveSequence(ArpeggiatorSettings* settings) {
	if (settings->numOctaves == 1) {
		currentOctave = 0;
		currentOctaveDirection = 1;
	}
	else if (settings->octaveMode == ArpOctaveMode::RANDOM) {
		currentOctave = getRandom255() % settings->numOctaves;
		currentOctaveDirection = 1;
	}
	else if (settings->octaveMode == ArpOctaveMode::UP_DOWN || settings->octaveMode == ArpOctaveMode::ALTERNATE) {
		currentOctave += currentOctaveDirection;
		if (currentOctave > settings->numOctaves - 1) {
			// Now go down
			currentOctaveDirection = -1;
			if (settings->octaveMode == ArpOctaveMode::ALTERNATE) {
				currentOctave = settings->numOctaves - 2;
			}
			else {
				currentOctave = settings->numOctaves - 1;
			}
		}
		else if (currentOctave < 0) {
			// Now go up
			currentOctaveDirection = 1;
			if (settings->octaveMode == ArpOctaveMode::ALTERNATE) {
				currentOctave = 1;
			}
			else {
				currentOctave = 0;
			}
		}
	}
	else {
		// Have to reset this, in case the user changed the setting.
		currentOctaveDirection = (settings->octaveMode == ArpOctaveMode::DOWN) ? -1 : 1;
		currentOctave += currentOctaveDirection;
		if (currentOctave >= settings->numOctaves) {
			currentOctave = 0;
		}
		else if (currentOctave < 0) {
			currentOctave = settings->numOctaves - 1;
		}
	}
}

// Increase sequence and rhythm indexes
void ArpeggiatorBase::increaseSequenceAndRhythmIndexes() {
	if (maxSequenceLength > 0) {
		notesPlayedFromSequence++;
	}
	notesPlayedFromRhythm = (notesPlayedFromRhythm + 1) % arpRhythmPatterns[rhythm].length;
	randomNotesPlayedFromOctave++;
}

void ArpeggiatorForDrum::calculateNextOctave(ArpeggiatorSettings* settings) {
	// If RANDOM, we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->octaveMode == ArpOctaveMode::RANDOM) {
		currentOctave = getRandom255() % settings->numOctaves;
		currentOctaveDirection = 1;
		// Must set all these variables here, even though RANDOM
		// doesn't use them, in case user changes arp mode.
		currentOctaveDirection = 1;
	}
	// Or not RANDOM
	else {
		// If which-note not actually set up yet...
		if (!playedFirstArpeggiatedNoteYet) {
			// Set the initial octave
			setInitialOctave(settings);
		}
		// Otherwise, just carry on the sequence of arpeggiated notes
		else {
			carryOnOctaveSequence(settings);
		}
	}
}

// Set initial values for note and octave
void ArpeggiatorForDrum::setInitialOctave(ArpeggiatorSettings* settings) {
	// OCTAVE
	if (settings->octaveMode == ArpOctaveMode::DOWN) {
		currentOctave = settings->numOctaves - 1;
		currentOctaveDirection = -1;
	}
	else {
		currentOctave = 0;
		currentOctaveDirection = 1;
	}
}

void ArpeggiatorForDrum::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction,
                                      bool isRatchet) {
	// Note: for drum arpeggiator, the note mode is irrelevant, so we don't need to check it here.
	//  We only need to account for octaveMode as it is always a 1-note arpeggio.
	//  Besides, behavior of OctaveMode::UP_DOWN is equal to OctaveMode::ALTERNATE
	if (!isRatchet
	    && (!playedFirstArpeggiatedNoteYet
	        || (maxSequenceLength > 0 && notesPlayedFromSequence >= maxSequenceLength))) {
		// Reset first note flag
		playedFirstArpeggiatedNoteYet = false;
		// Reset indexes
		notesPlayedFromSequence = 0;
		notesPlayedFromRhythm = 0;
		lastNormalNotePlayedFromRhythm = 0;
	}

	bool shouldCarryOnRhythmNote = evaluateRhythm(isRatchet);

	if (isRatchet) {
		// Increment ratchet note index if we are ratcheting when entering switchNoteOn
		ratchetNotesIndex++;
	}
	else {
		// If this is a normal switchNoteOn event (not ratchet, and not silent) we take here the opportunity to setup a
		// ratchet burst and also make all the necessary calculations for the next note to be played
		if (shouldCarryOnRhythmNote) {
			// Setup ratchet
			maybeSetupNewRatchet(settings);

			// Move indexes to the next note in the sequence
			calculateNextOctave(settings);

			// As we have set a note and an octave, we can say we have played a note
			playedFirstArpeggiatedNoteYet = true;

			// Save last note played from rhythm
			lastNormalNotePlayedFromRhythm = notesPlayedFromRhythm;
		}

		// Increase steps played from the sequence or rhythm for both silent and non-silent notes
		increaseSequenceAndRhythmIndexes();
	}

	if (shouldCarryOnRhythmNote) {
		// Set Gate as active
		gateCurrentlyActive = true;
		if (!isRatchet) {
			// Reset gate position (only for normal notes)
			gatePos = 0;
		}

		// Check if we need to update velocity with some MPE value
		switch (settings->mpeVelocity) {
		case ArpMpeModSource::AFTERTOUCH:
			arpNote.velocity = arpNote.mpeValues[util::to_underlying(Expression::Z_PRESSURE)] >> 8;
			break;
		case ArpMpeModSource::MPE_Y:
			arpNote.velocity =
			    ((arpNote.mpeValues[util::to_underlying(Expression::Y_SLIDE_TIMBRE)] >> 1) + (1 << 14)) >> 8;
			break;
		}
		// Fix velocity if it's too low
		if (arpNote.velocity < MIN_MPE_MODULATED_VELOCITY) {
			arpNote.velocity = MIN_MPE_MODULATED_VELOCITY;
		}
		// Play the note
		noteCodeCurrentlyOnPostArp = kNoteForDrum + (int32_t)currentOctave * 12;
		instruction->noteCodeOnPostArp = noteCodeCurrentlyOnPostArp;
		instruction->arpNoteOn = &arpNote;
	}
	else {
		// Rhythm silence: Don't play the note
		instruction->noteCodeOnPostArp = ARP_NOTE_NONE;
	}
}

void Arpeggiator::calculateNextNoteAndOrOctave(ArpeggiatorSettings* settings) {
	// If FULL-RANDOM (RANDOM for both Note and Octave)
	// we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->noteMode == ArpNoteMode::RANDOM && settings->octaveMode == ArpOctaveMode::RANDOM) {
		whichNoteCurrentlyOnPostArp = getRandom255() % (uint8_t)notes.getNumElements();
		currentOctave = getRandom255() % settings->numOctaves;
		// Must set all these variables here, even though RANDOM
		// doesn't use them, in case user changes arp mode.
		currentOctaveDirection = 1;
		randomNotesPlayedFromOctave = 0;
		currentDirection = 1;
	}

	// Or not FULL-RANDOM
	else {
		// If which-note not actually set up yet...
		if (!playedFirstArpeggiatedNoteYet) {
			// Set the initial note and octave values and directions
			setInitialNoteAndOctave(settings);
		}

		// For 1-note arpeggios it is simpler and can use the same logic as for drums
		else if (notes.getNumElements() == 1) {
			carryOnOctaveSequence(settings);
		}

		// Otherwise, just carry on the sequence of arpeggiated notes
		else {

			// Arpeggios of more than 1 note

			// NOTE
			bool changeOctave = false;
			bool changingOctaveDirection = false;
			if (settings->noteMode == ArpNoteMode::RANDOM) {
				// Note: Random
				whichNoteCurrentlyOnPostArp = getRandom255() % (uint8_t)notes.getNumElements();
				if (randomNotesPlayedFromOctave >= notes.getNumElements()) {
					changeOctave = true;
				}
			}
			else {
				whichNoteCurrentlyOnPostArp += currentDirection;

				// If reached top of notes (so current direction must be up)
				if (whichNoteCurrentlyOnPostArp >= notes.getNumElements()) {
					changingOctaveDirection =
					    (int32_t)currentOctave >= settings->numOctaves - 1
					    && (settings->noteMode == ArpNoteMode::UP || settings->noteMode == ArpNoteMode::AS_PLAYED
					        || settings->noteMode == ArpNoteMode::DOWN)
					    && settings->octaveMode == ArpOctaveMode::ALTERNATE;
					if (changingOctaveDirection) {
						// Now go down (without repeating)
						currentDirection = -1;
						whichNoteCurrentlyOnPostArp = notes.getNumElements() - 2;
					}
					else if (settings->noteMode == ArpNoteMode::UP_DOWN) {
						// Now go down (repeating note)
						currentDirection = -1;
						whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
					}
					else { // Up or AsPlayed
						// Start on next octave first note
						whichNoteCurrentlyOnPostArp = 0;
						changeOctave = true;
					}
				}

				// Or, if reached bottom of notes (so current direction must be down)
				else if (whichNoteCurrentlyOnPostArp < 0) {
					changingOctaveDirection =
					    (int32_t)currentOctave <= 0
					    && (settings->noteMode == ArpNoteMode::UP || settings->noteMode == ArpNoteMode::AS_PLAYED
					        || settings->noteMode == ArpNoteMode::DOWN)
					    && settings->octaveMode == ArpOctaveMode::ALTERNATE;
					if (changingOctaveDirection) {
						// Now go up
						currentDirection = 1;
						whichNoteCurrentlyOnPostArp = 1;
					}
					else if (settings->noteMode == ArpNoteMode::UP_DOWN) { // UpDown
						// Start on next octave first note
						whichNoteCurrentlyOnPostArp = 0;
						currentDirection = 1;
						changeOctave = true;
					}
					else { // Down
						whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
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
				carryOnOctaveSequence(settings);
			}
		}
	}
}

// Set initial values for note and octave
void Arpeggiator::setInitialNoteAndOctave(ArpeggiatorSettings* settings) {
	// NOTE
	if (settings->noteMode == ArpNoteMode::RANDOM) {
		// Note: Random
		whichNoteCurrentlyOnPostArp = getRandom255() % (uint8_t)notes.getNumElements();
		currentDirection = 1;
	}
	else if (settings->noteMode == ArpNoteMode::DOWN) {
		// Note: Down
		whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
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

void Arpeggiator::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction, bool isRatchet) {
	if (!isRatchet
	    && (!playedFirstArpeggiatedNoteYet
	        || (maxSequenceLength > 0 && notesPlayedFromSequence >= maxSequenceLength))) {
		// Reset first note flag
		playedFirstArpeggiatedNoteYet = false;
		// Reset indexes
		notesPlayedFromSequence = 0;
		notesPlayedFromRhythm = 0;
		lastNormalNotePlayedFromRhythm = 0;
		randomNotesPlayedFromOctave = 0;
		whichNoteCurrentlyOnPostArp = 0;
	}

	bool shouldCarryOnRhythmNote = evaluateRhythm(isRatchet);

	if (isRatchet) {
		// Increment ratchet note index if we are ratcheting when entering switchNoteOn
		ratchetNotesIndex++;
	}
	else {
		// If this is a normal switchNoteOn event (not ratchet, and not silent) we take here the opportunity to setup a
		// ratchet burst and also make all the necessary calculations for the next note to be played
		if (shouldCarryOnRhythmNote) {
			// Setup ratchet
			maybeSetupNewRatchet(settings);

			// Move indexes to the next note in the sequence
			calculateNextNoteAndOrOctave(settings);

			// As we have set a note and an octave, we can say we have played a note
			playedFirstArpeggiatedNoteYet = true;

			// Save last note played from rhythm
			lastNormalNotePlayedFromRhythm = notesPlayedFromRhythm;
		}

		// Increase steps played from the sequence or rhythm for both silent and non-silent notes
		increaseSequenceAndRhythmIndexes();
	}

	// Clamp the index to real range
	whichNoteCurrentlyOnPostArp = std::clamp<int16_t>(whichNoteCurrentlyOnPostArp, 0, notes.getNumElements() - 1);

	ArpNote* arpNote;
	if (settings->noteMode == ArpNoteMode::AS_PLAYED) {
		arpNote = (ArpNote*)notesAsPlayed.getElementAddress(whichNoteCurrentlyOnPostArp);
	}
	else {
		arpNote = (ArpNote*)notes.getElementAddress(whichNoteCurrentlyOnPostArp);
	}

	if (shouldCarryOnRhythmNote) {
		// Set Gate as active
		gateCurrentlyActive = true;
		if (!isRatchet) {
			// Reset gate position (only for normal notes)
			gatePos = 0;
		}

		// Check if we need to update velocity with some MPE value
		switch (settings->mpeVelocity) {
		case ArpMpeModSource::AFTERTOUCH:
			arpNote->velocity = arpNote->mpeValues[util::to_underlying(Expression::Z_PRESSURE)] >> 8;
			break;
		case ArpMpeModSource::MPE_Y:
			arpNote->velocity = arpNote->mpeValues[util::to_underlying(Expression::Y_SLIDE_TIMBRE)] >> 8;
			break;
		}
		// Fix velocity if it's too low
		if (arpNote->velocity < MIN_MPE_MODULATED_VELOCITY) {
			arpNote->velocity = MIN_MPE_MODULATED_VELOCITY;
		}
		// Play the note
		noteCodeCurrentlyOnPostArp =
		    arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] + (int32_t)currentOctave * 12;
		instruction->noteCodeOnPostArp = noteCodeCurrentlyOnPostArp;
		instruction->arpNoteOn = arpNote;
	}
	else {
		// Rhythm silence: Don't play the note
		instruction->noteCodeOnPostArp = ARP_NOTE_NONE;
	}
}

bool Arpeggiator::hasAnyInputNotesActive() {
	return notes.getNumElements();
}

bool ArpeggiatorForDrum::hasAnyInputNotesActive() {
	return arpNote.velocity;
}

void ArpeggiatorBase::updateParams(uint32_t sequenceLength, uint32_t rhythmValue, uint32_t ratchAmount,
                                   uint32_t ratchProb) {
	// Update live Sequence Length value with the most up to date value from automation
	maxSequenceLength = computeCurrentValueForArpMidiCvRatchetsOrRhythm(sequenceLength);

	// Update live Sequence Length value with the most up to date value from automation
	rhythm = computeCurrentValueForArpMidiCvRatchetsOrRhythm(rhythmValue);

	// Update live ratchetProbability value with the most up to date value from automation
	ratchetProbability = ratchProb >> 16; // just 16 bits is enough resolution for probability

	// Update live ratchetAmount value with the most up to date value from automation
	// Convert ratchAmount to either 0, 1, 2 or 3 (equivalent to a number of ratchets: OFF, 2, 4, 8)
	uint16_t amount = ratchAmount >> 16;
	if (amount > 45874) { // > 35 -> 50
		ratchetAmount = 3;
	}
	else if (amount > 26214) { // > 20 -> 34
		ratchetAmount = 2;
	}
	else if (amount > 6553) { // > 5 -> 19
		ratchetAmount = 1;
	}
	else { // > 0 -> 4
		ratchetAmount = 0;
	}
}

// Check arpeggiator is on before you call this.
// May switch notes on and/or off.
void ArpeggiatorBase::render(ArpeggiatorSettings* settings, int32_t numSamples, uint32_t gateThreshold,
                             uint32_t phaseIncrement, uint32_t sequenceLength, uint32_t rhythmValue,
                             uint32_t ratchAmount, uint32_t ratchProb, ArpReturnInstruction* instruction) {
	if (settings->mode == ArpMode::OFF || !hasAnyInputNotesActive()) {
		return;
	}

	updateParams(sequenceLength, rhythmValue, ratchAmount, ratchProb);

	if (settings->flagForceArpRestart) {
		// If flagged to restart sequence, do it now and reset the flag
		playedFirstArpeggiatedNoteYet = false;
		settings->flagForceArpRestart = false;
	}

	uint32_t gateThresholdSmall = gateThreshold >> 8;

	if (isRatcheting) {
		// shorten gate in case we are ratcheting (with the calculated number of ratchet notes)
		gateThresholdSmall = gateThresholdSmall >> ratchetNotesMultiplier;
	}

	uint32_t maxGate = 1 << 24;
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
