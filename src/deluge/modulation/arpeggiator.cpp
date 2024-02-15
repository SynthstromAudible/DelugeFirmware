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
#include "io/debug/log.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"
#include "util/functions.h"
#include <stdint.h>

ArpeggiatorSettings::ArpeggiatorSettings() {
	numOctaves = 2;
	numRatchets = 0;
	mode = ArpMode::OFF;

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

Arpeggiator::Arpeggiator() : notes(sizeof(ArpNote), 16, 0, 8, 8) {
	notes.emptyingShouldFreeMemory = false;
}

// Surely this shouldn't be quite necessary?
void ArpeggiatorForDrum::reset() {
	arpNote.velocity = 0;
}

void ArpeggiatorBase::resetRatchet() {
	ratchetNotesIndex = 0;
	ratchetNotesMultiplier = 0;
	ratchetNotesNumber = 0;
	isRatcheting = false;
	D_PRINTLN("i %d m %d n %d b %d -> resetRatchet", ratchetNotesIndex, ratchetNotesMultiplier, ratchetNotesNumber,
	          isRatcheting);
}

void Arpeggiator::reset() {
	notes.empty();

	D_PRINTLN("Arpeggiator::reset");
	resetRatchet();
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
				gatePos = 0;
				switchNoteOn(settings, instruction);
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

	int32_t n = notes.search(noteCode, GREATER_OR_EQUAL);

	ArpNote* arpNote;

	// If it already exists...
	if (n < notes.getNumElements()) {
		arpNote = (ArpNote*)notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCode) {
			if ((settings != nullptr) && settings->mode != ArpMode::OFF) {
				return; // If we're an arpeggiator, return
			}
			else {
				goto noteInserted; // Otherwise, try again. Actually is this really useful?
			}
		}
	}

	// Insert
	{
		int32_t error = notes.insertAtIndex(n);
		if (error) {
			return;
		}
	}

	arpNote = (ArpNote*)notes.getElementAddress(n);

	arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = noteCode;
	arpNote->velocity = velocity;
	arpNote->outputMemberChannel =
	    MIDI_CHANNEL_NONE; // MIDIInstrument might set this, but it needs to be MIDI_CHANNEL_NONE until then so it
	                       // doesn't get included in the survey that will happen of existing output member channels.

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		arpNote->mpeValues[m] = mpeValues[m];
	}

noteInserted:
	arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] =
	    fromMIDIChannel; // This is here so that "stealing" a note being edited can then replace its MPE data during
	                     // editing. Kind of a hacky solution, but it works for now.

	// If we're an arpeggiator...
	if ((settings != nullptr) && settings->mode != ArpMode::OFF) {

		// If this was the first note-on and we want to sound a note right now...
		if (notes.getNumElements() == 1) {
			playedFirstArpeggiatedNoteYet = false;
			gateCurrentlyActive = false;

			if (!(playbackHandler.isEitherClockActive()) || !settings->syncLevel) {
				gatePos = 0;
				switchNoteOn(settings, instruction);
			}
		}

		// Or if the arpeggiator was already sounding
		else {
			if (whichNoteCurrentlyOnPostArp >= n) {
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

	int32_t n = notes.search(noteCodePreArp, GREATER_OR_EQUAL);
	if (n < notes.getNumElements()) {

		ArpNote* arpNote = (ArpNote*)notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] == noteCodePreArp) {

			// If no arpeggiation...
			if ((settings == nullptr) || settings->mode == ArpMode::OFF) {
				instruction->noteCodeOffPostArp = noteCodePreArp;
				instruction->outputMIDIChannelOff = arpNote->outputMemberChannel;
			}

			// Or if yes arpeggiation, we'll only stop right now if that was the last note to switch off. Otherwise,
			// it'll turn off soon with the arpeggiation.
			else {
				if (notes.getNumElements() == 1) {
					if (whichNoteCurrentlyOnPostArp == n && gateCurrentlyActive) {
						instruction->noteCodeOffPostArp = noteCodeCurrentlyOnPostArp;
						instruction->outputMIDIChannelOff = outputMIDIChannelForNoteCurrentlyOnPostArp;
					}
				}
			}

			notes.deleteAtIndex(n);

			if (whichNoteCurrentlyOnPostArp >= n) {
				whichNoteCurrentlyOnPostArp--; // Beware - this could send it negative
				if (whichNoteCurrentlyOnPostArp < 0) {
					whichNoteCurrentlyOnPostArp = 0;
				}
			}

			if (isRatcheting && (ratchetNotesIndex >= ratchetNotesNumber || !playbackHandler.isEitherClockActive())) {
				// If it was ratcheting but it reached the last note in the ratchet or play was stopped
				// then we can reset the ratchet temp values.
				D_PRINTLN("noteOff 1");
				resetRatchet();
			}
		}
	}

	if (notes.getNumElements() == 0) {
		D_PRINTLN("noteOff 2");
		resetRatchet();
	}
}

void ArpeggiatorBase::switchAnyNoteOff(ArpReturnInstruction* instruction) {
	if (gateCurrentlyActive) {
		// triggerable->noteOffPostArpeggiator(modelStack, noteCodeCurrentlyOnPostArp);
		instruction->noteCodeOffPostArp = noteCodeCurrentlyOnPostArp;
		instruction->outputMIDIChannelOff = outputMIDIChannelForNoteCurrentlyOnPostArp;
		gateCurrentlyActive = false;

		if (isRatcheting && (ratchetNotesIndex >= ratchetNotesNumber || !playbackHandler.isEitherClockActive())) {
			// If it was ratcheting but it reached the last note in the ratchet or play was stopped
			// then we can reset the ratchet temp values.
			D_PRINTLN("switchAnyNoteOff");
			resetRatchet();
		}
	}
}

void ArpeggiatorBase::maybeSetupNewRatchet(ArpeggiatorSettings* settings) {
	int32_t randomChance = random(65535);
	isRatcheting = ratchetProbability > randomChance && settings->numRatchets > 0;
	if (isRatcheting) {
		ratchetNotesMultiplier = 1 + (random(65535) % settings->numRatchets);
		ratchetNotesNumber = 1 << ratchetNotesMultiplier;
		if (settings->syncLevel == SyncLevel::SYNC_LEVEL_256TH) {
			// If the sync level is 256th, we can't have a ratchet of more than 2 notes, so we set it to the minimum
			ratchetNotesMultiplier = 1;
			ratchetNotesNumber = 2;
		}
		else if (settings->syncLevel == SyncLevel::SYNC_LEVEL_128TH) {
			// If the sync level is 128th, the maximum ratchet can be of 4 notes (8 not allowed)
			ratchetNotesMultiplier = std::max((uint8_t)2, ratchetNotesMultiplier);
			ratchetNotesNumber = std::max((uint8_t)4, ratchetNotesNumber);
		}
	}
	else {
		ratchetNotesMultiplier = 0;
		ratchetNotesNumber = 0;
	}
	ratchetNotesIndex = 0;
	D_PRINTLN("i %d m %d n %d b %d -> maybeSetupNewRatchet", ratchetNotesIndex, ratchetNotesMultiplier,
	          ratchetNotesNumber, isRatcheting);
}

int32_t ArpeggiatorBase::getOctaveDirection(ArpeggiatorSettings* settings) {
	switch (settings->octaveMode) {
	case ArpOctaveMode::RANDOM:
		// Random (can be up or down)
		return getRandom255() % 2 == 0 ? 1 : -1;
	case ArpOctaveMode::DOWN:
		// Down
		return -1;
	default:
		// Up
		return 1;
	}
}

void ArpeggiatorForDrum::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) {

	gateCurrentlyActive = true;

	// If RANDOM, we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->mode == ArpMode::RANDOM) {
		currentOctave = getRandom255() % settings->numOctaves;
	}

	// Or not RANDOM
	else {

		// If which-note not actually set up yet...
		if (!playedFirstArpeggiatedNoteYet) {

			if (settings->mode == ArpMode::DOWN) {
				currentOctave = settings->numOctaves - 1;
				currentDirection = -1;
			}
			else {
				currentOctave = 0;
				currentDirection = 1;
			}
		}

		// Otherwise, just carry on the sequence of arpeggiated notes
		else {

			if (settings->mode == ArpMode::BOTH) {

				if (settings->numOctaves == 1) {
					currentOctave = 0;
				}
				else {
					if (currentOctave >= settings->numOctaves - 1) {
						currentDirection = -1;
					}

					else if (currentOctave <= 0) {
						currentDirection = 1;
					}
				}
				currentOctave += currentDirection;
			}

			else {
				currentDirection = (settings->mode == ArpMode::DOWN)
				                       ? -1
				                       : 1; // Have to reset this, in case the user changed the setting.
				currentOctave += currentDirection;
				if (currentOctave >= settings->numOctaves) {
					currentOctave = 0;
				}
				else if (currentOctave < 0) {
					currentOctave = settings->numOctaves - 1;
				}
			}
		}
	}

	// For drums as it is just one note, the octave change happens on every note
	currentOctaveDirection = getOctaveDirection(settings);

	playedFirstArpeggiatedNoteYet = true;

	noteCodeCurrentlyOnPostArp = kNoteForDrum + (int32_t)currentOctave * 12 * currentOctaveDirection;

	instruction->noteCodeOnPostArp = noteCodeCurrentlyOnPostArp;
	instruction->arpNoteOn = &arpNote;
}

void Arpeggiator::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) {

	gateCurrentlyActive = true;

	if (ratchetNotesIndex > 0) {
		goto finishSwitchNoteOn;
	}

	// If RANDOM, we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->mode == ArpMode::RANDOM) {
		whichNoteCurrentlyOnPostArp = getRandom255() % (uint8_t)notes.getNumElements();
		currentOctave = getRandom255() % settings->numOctaves;
		currentDirection = 1; // Must set a currentDirection here, even though RANDOM doesn't use it, in case user
		                      // changes arp mode.
		currentOctaveDirection = getOctaveDirection(settings);
	}

	// Or not RANDOM
	else {

		// If which-note not actually set up yet...
		if (!playedFirstArpeggiatedNoteYet) {
			if (settings->mode == ArpMode::DOWN) {
				whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
				currentOctave = settings->numOctaves - 1;
				currentDirection = -1;
			}
			else {
				whichNoteCurrentlyOnPostArp = 0;
				currentOctave = 0;
				currentDirection = 1;
			}
			currentOctaveDirection = getOctaveDirection(settings);
		}

		// Otherwise, just carry on the sequence of arpeggiated notes
		else {

			whichNoteCurrentlyOnPostArp += currentDirection;

			// If reached top of notes (so current direction must be up)
			if (whichNoteCurrentlyOnPostArp >= notes.getNumElements()) {

				// If at top octave
				if ((int32_t)currentOctave >= settings->numOctaves - 1) {

					if (settings->mode == ArpMode::UP) {
						whichNoteCurrentlyOnPostArp -= notes.getNumElements();
						currentOctave = 0;
						currentOctaveDirection = getOctaveDirection(settings);
					}

					else { // Up+down
						currentDirection = -1;
						whichNoteCurrentlyOnPostArp -= 2;
						if (whichNoteCurrentlyOnPostArp < 0) {
							whichNoteCurrentlyOnPostArp = 0;
							if (currentOctave > 0) {
								currentOctave--;
								currentOctaveDirection = getOctaveDirection(settings);
							}
						}
					}
				}

				// Otherwise, just continue
				else {
					whichNoteCurrentlyOnPostArp -= notes.getNumElements();
					currentOctave++;
					currentOctaveDirection = getOctaveDirection(settings);
				}
			}

			// Or, if reached bottom of notes (so current direction must be down)
			if (whichNoteCurrentlyOnPostArp < 0) {

				// If at bottom octave
				if (currentOctave <= 0) {

					if (settings->mode == ArpMode::DOWN) {
						whichNoteCurrentlyOnPostArp += notes.getNumElements();
						currentOctave = settings->numOctaves - 1;
						currentOctaveDirection = getOctaveDirection(settings);
					}

					else { // Up+down
						currentDirection = 1;
						whichNoteCurrentlyOnPostArp += 2;
						if (whichNoteCurrentlyOnPostArp >= notes.getNumElements()) {
							whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
							if (currentOctave < settings->numOctaves - 1) {
								currentOctave++;
								currentOctaveDirection = getOctaveDirection(settings);
							}
						}
					}
				}

				// Otherwise, just continue
				else {
					whichNoteCurrentlyOnPostArp += notes.getNumElements();
					currentOctave--;
					currentOctaveDirection = getOctaveDirection(settings);
				}
			}
		}
	}

finishSwitchNoteOn:
	playedFirstArpeggiatedNoteYet = true;

#if ALPHA_OR_BETA_VERSION
	if (whichNoteCurrentlyOnPostArp < 0 || whichNoteCurrentlyOnPostArp >= notes.getNumElements()) {
		FREEZE_WITH_ERROR("E404");
	}
#endif

	ArpNote* arpNote = (ArpNote*)notes.getElementAddress(whichNoteCurrentlyOnPostArp);

	noteCodeCurrentlyOnPostArp = arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)]
	                             + (int32_t)currentOctave * 12 * currentOctaveDirection;

	instruction->noteCodeOnPostArp = noteCodeCurrentlyOnPostArp;
	instruction->arpNoteOn = arpNote;

	// Increment ratchet note index if we are ratcheting
	if (isRatcheting) {
		ratchetNotesIndex++;
		D_PRINTLN("i %d m %d n %d b %d -> switchNoteOn RATCHETING", ratchetNotesIndex, ratchetNotesMultiplier,
		          ratchetNotesNumber, isRatcheting);
	}
	else {
		D_PRINTLN("i %d m %d n %d b %d -> switchNoteOn NORMAL", ratchetNotesIndex, ratchetNotesMultiplier,
		          ratchetNotesNumber, isRatcheting);
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
void ArpeggiatorBase::render(ArpeggiatorSettings* settings, int32_t numSamples, uint32_t gateThreshold,
                             uint32_t phaseIncrement, uint32_t chance, ArpReturnInstruction* instruction) {
	if (settings->mode == ArpMode::OFF || !hasAnyInputNotesActive()) {
		return;
	}

	uint32_t gateThresholdSmall = gateThreshold >> 8;

	// Update ratchetProbability with the most up to date value from automation
	ratchetProbability = chance >> 16; // just 16 bits is enough resolution for probability

	if (isRatcheting) {
		// shorten gate in case we are ratcheting (with the calculated number of ratchet notes)
		gateThresholdSmall = gateThresholdSmall >> ratchetNotesMultiplier;
	}

	bool syncedNow = (settings->syncLevel && (playbackHandler.isEitherClockActive()));

	// If gatePos is far enough along that we at least want to switch off any note...
	if (gatePos >= gateThresholdSmall) {
		switchAnyNoteOff(instruction);

		// And maybe (if not syncing) the gatePos is also far enough along that we also want to switch a note on?
		if (!syncedNow && gatePos >= 16777216) {
			switchNoteOn(settings, instruction);
		}
	}

	if (!syncedNow) {
		gatePos &= 16777215;
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

	if (ratchetingIsAvailable) {
		if (!isRatcheting) {
			// If we are not ratcheting yet, check if we should and set it up (based on ratchet chance)
			maybeSetupNewRatchet(settings);
		}

		// If in previous step we set up ratcheting, we need to recalculate ticksPerPeriod
		if (isRatcheting) {
			ticksPerPeriod = ticksPerPeriod >> ratchetNotesMultiplier;
		}
	}

	int32_t howFarIntoPeriod = clipCurrentPos % ticksPerPeriod;

	if (!howFarIntoPeriod) {
		if (hasAnyInputNotesActive()) {
			switchAnyNoteOff(instruction);
			switchNoteOn(settings, instruction);

			instruction->sampleSyncLengthOn = ticksPerPeriod; // Overwrite this
			gatePos = 0;
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
	}
	return phaseIncrement;
}
