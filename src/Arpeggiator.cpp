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

#include "Arpeggiator.h"
#include <string.h>
#include "playbackhandler.h"
#include "functions.h"
#include "song.h"
#include "ModelStack.h"
#include "FlashStorage.h"

ArpeggiatorSettings::ArpeggiatorSettings() {
	numOctaves = 2;
	mode = ARP_MODE_OFF;

	// I'm so sorry, this is incredibly ugly, but in order to decide the default sync level, we have to look at the current song, or even better the one being preloaded.
	// Default sync level is used obviously for the default synth sound if no SD card inserted, but also some synth presets, possibly just older ones,
	// are saved without this so it can be set to the default at the time of loading.
	Song* song = preLoadedSong;
	if (!song) song = currentSong;
	if (song) {
		syncLevel = 8 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM);
	}
	else {
		syncLevel = 8 - FlashStorage::defaultMagnitude;
	}
}

ArpeggiatorForDrum::ArpeggiatorForDrum() {
	arpNote.velocity = 0;
}

Arpeggiator::Arpeggiator() : notes(sizeof(ArpNote), 16, 0, 8, 8) {
	notes.emptyingShouldFreeMemory = false;
}

// Surely this shouldn't be quite necessary?
void ArpeggiatorForDrum::reset() {
	arpNote.velocity = 0;
}

void Arpeggiator::reset() {
	notes.empty();
}

void ArpeggiatorForDrum::noteOn(ArpeggiatorSettings* settings, int noteCode, int velocity,
                                ArpReturnInstruction* instruction, int fromMIDIChannel, int16_t const* mpeValues) {
	lastVelocity = velocity;

	bool wasActiveBefore = arpNote.velocity;

	arpNote.inputCharacteristics[MIDI_CHARACTERISTIC_NOTE] = noteCode;
	arpNote.inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL] = fromMIDIChannel;
	arpNote.velocity = velocity; // Means note is on.
	// MIDIInstrument might set this later, but it needs to be MIDI_CHANNEL_NONE until then so it doesn't get included
	// in the survey that will happen of existing output member channels.
	arpNote.outputMemberChannel = MIDI_CHANNEL_NONE;

	for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
		arpNote.mpeValues[m] = mpeValues[m];
	}

	// If we're an actual arpeggiator...
	if (settings && settings->mode) {

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
	if (!settings || !settings->mode) {
		instruction->noteCodeOffPostArp = NOTE_FOR_DRUM;
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

// May return the instruction for a note-on, or no instruction. The noteCode instructed might be some octaves up from that provided here.
void Arpeggiator::noteOn(ArpeggiatorSettings* settings, int noteCode, int velocity, ArpReturnInstruction* instruction,
                         int fromMIDIChannel, int16_t const* mpeValues) {

	lastVelocity = velocity;

	int n = notes.search(noteCode, GREATER_OR_EQUAL);

	ArpNote* arpNote;

	// If it already exists...
	if (n < notes.getNumElements()) {
		arpNote = (ArpNote*)notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE] == noteCode) {
			if (settings && settings->mode) return; // If we're an arpeggiator, return
			else goto noteInserted;                 // Otherwise, try again. Actually is this really useful?
		}
	}

	// Insert
	{
		int error = notes.insertAtIndex(n);
		if (error) return;
	}

	arpNote = (ArpNote*)notes.getElementAddress(n);

	arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE] = noteCode;
	arpNote->velocity = velocity;
	arpNote->outputMemberChannel =
	    MIDI_CHANNEL_NONE; // MIDIInstrument might set this, but it needs to be MIDI_CHANNEL_NONE until then so it doesn't get included in the survey that will happen of existing output member channels.

	for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
		arpNote->mpeValues[m] = mpeValues[m];
	}

noteInserted:
	arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL] =
	    fromMIDIChannel; // This is here so that "stealing" a note being edited can then replace its MPE data during editing. Kind of a hacky solution, but it works for now.

	// If we're an arpeggiator...
	if (settings && settings->mode) {

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

void Arpeggiator::noteOff(ArpeggiatorSettings* settings, int noteCodePreArp, ArpReturnInstruction* instruction) {

	int n = notes.search(noteCodePreArp, GREATER_OR_EQUAL);
	if (n < notes.getNumElements()) {

		ArpNote* arpNote = (ArpNote*)notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE] == noteCodePreArp) {

			// If no arpeggiation...
			if (!settings || !settings->mode) {
				instruction->noteCodeOffPostArp = noteCodePreArp;
				instruction->outputMIDIChannelOff = arpNote->outputMemberChannel;
			}

			// Or if yes arpeggiation, we'll only stop right now if that was the last note to switch off. Otherwise, it'll turn off soon with the arpeggiation.
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
				if (whichNoteCurrentlyOnPostArp < 0) whichNoteCurrentlyOnPostArp = 0;
			}
		}
	}
}

void ArpeggiatorBase::switchAnyNoteOff(ArpReturnInstruction* instruction) {
	if (gateCurrentlyActive) {
		//triggerable->noteOffPostArpeggiator(modelStack, noteCodeCurrentlyOnPostArp);
		instruction->noteCodeOffPostArp = noteCodeCurrentlyOnPostArp;
		instruction->outputMIDIChannelOff = outputMIDIChannelForNoteCurrentlyOnPostArp;
		gateCurrentlyActive = false;
	}
}

void ArpeggiatorForDrum::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) {

	gateCurrentlyActive = true;

	// If RANDOM, we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->mode == ARP_MODE_RANDOM) {
		currentOctave = getRandom255() % settings->numOctaves;
	}

	// Or not RANDOM
	else {

		// If which-note not actually set up yet...
		if (!playedFirstArpeggiatedNoteYet) {

			if (settings->mode == ARP_MODE_DOWN) {
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

			if (settings->mode == ARP_MODE_BOTH) {

				if (settings->numOctaves == 1) currentOctave = 0;

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
				currentDirection = (settings->mode == ARP_MODE_DOWN)
				                       ? -1
				                       : 1; // Have to reset this, in case the user changed the setting.
				currentOctave += currentDirection;
				if (currentOctave >= settings->numOctaves) currentOctave = 0;
				else if (currentOctave < 0) currentOctave = settings->numOctaves - 1;
			}
		}
	}

	playedFirstArpeggiatedNoteYet = true;

	noteCodeCurrentlyOnPostArp = NOTE_FOR_DRUM + (int)currentOctave * currentSong->octaveNumMicrotonalNotes;

	instruction->noteCodeOnPostArp = noteCodeCurrentlyOnPostArp;
	instruction->arpNoteOn = &arpNote;
}

void Arpeggiator::switchNoteOn(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction) {

	gateCurrentlyActive = true;

	// If RANDOM, we do the same thing whether playedFirstArpeggiatedNoteYet or not
	if (settings->mode == ARP_MODE_RANDOM) {
		whichNoteCurrentlyOnPostArp = getRandom255() % (uint8_t)notes.getNumElements();
		currentOctave = getRandom255() % settings->numOctaves;
		currentDirection =
		    1; // Must set a currentDirection here, even though RANDOM doesn't use it, in case user changes arp mode.
	}

	// Or not RANDOM
	else {

		// If which-note not actually set up yet...
		if (!playedFirstArpeggiatedNoteYet) {

			if (settings->mode == ARP_MODE_DOWN) {
				whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
				currentOctave = settings->numOctaves - 1;
				currentDirection = -1;
			}
			else {
				whichNoteCurrentlyOnPostArp = 0;
				currentOctave = 0;
				currentDirection = 1;
			}
		}

		// Otherwise, just carry on the sequence of arpeggiated notes
		else {

			whichNoteCurrentlyOnPostArp += currentDirection;

			// If reached top of notes (so current direction must be up)
			if (whichNoteCurrentlyOnPostArp >= notes.getNumElements()) {

				// If at top octave
				if ((int)currentOctave >= settings->numOctaves - 1) {

					if (settings->mode == ARP_MODE_UP) {
						whichNoteCurrentlyOnPostArp -= notes.getNumElements();
						currentOctave = 0;
					}

					else { // Up+down
						currentDirection = -1;
						whichNoteCurrentlyOnPostArp -= 2;
						if (whichNoteCurrentlyOnPostArp < 0) {
							whichNoteCurrentlyOnPostArp = 0;
							if (currentOctave > 0) currentOctave--;
						}
					}
				}

				// Otherwise, just continue
				else {
					whichNoteCurrentlyOnPostArp -= notes.getNumElements();
					currentOctave++;
				}
			}

			// Or, if reached bottom of notes (so current direction must be down)
			if (whichNoteCurrentlyOnPostArp < 0) {

				// If at bottom octave
				if (currentOctave <= 0) {

					if (settings->mode == ARP_MODE_DOWN) {
						whichNoteCurrentlyOnPostArp += notes.getNumElements();
						currentOctave = settings->numOctaves - 1;
					}

					else { // Up+down
						currentDirection = 1;
						whichNoteCurrentlyOnPostArp += 2;
						if (whichNoteCurrentlyOnPostArp >= notes.getNumElements()) {
							whichNoteCurrentlyOnPostArp = notes.getNumElements() - 1;
							if (currentOctave < settings->numOctaves - 1) currentOctave++;
						}
					}
				}

				// Otherwise, just continue
				else {
					whichNoteCurrentlyOnPostArp += notes.getNumElements();
					currentOctave--;
				}
			}
		}
	}

	playedFirstArpeggiatedNoteYet = true;

#if ALPHA_OR_BETA_VERSION
	if (whichNoteCurrentlyOnPostArp < 0 || whichNoteCurrentlyOnPostArp >= notes.getNumElements())
		numericDriver.freezeWithError("E404");
#endif

	ArpNote* arpNote = (ArpNote*)notes.getElementAddress(whichNoteCurrentlyOnPostArp);

	noteCodeCurrentlyOnPostArp = arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE] + (int)currentOctave * currentSong->octaveNumMicrotonalNotes;

	instruction->noteCodeOnPostArp = noteCodeCurrentlyOnPostArp;
	instruction->arpNoteOn = arpNote;
}

bool Arpeggiator::hasAnyInputNotesActive() {
	return notes.getNumElements();
}

bool ArpeggiatorForDrum::hasAnyInputNotesActive() {
	return arpNote.velocity;
}

// Check arpeggiator is on before you call this.
// May switch notes on and/or off.
void ArpeggiatorBase::render(ArpeggiatorSettings* settings, int numSamples, uint32_t gateThreshold,
                             uint32_t phaseIncrement, ArpReturnInstruction* instruction) {

	if (!settings->mode || !hasAnyInputNotesActive()) return;

	uint32_t gateThresholdSmall = gateThreshold >> 8;

	bool syncedNow = (settings->syncLevel && (playbackHandler.isEitherClockActive()));

	// If gatePos is far enough along that we at least want to switch off any note...
	if (gatePos >= gateThresholdSmall) {
		switchAnyNoteOff(instruction);

		// And maybe (if not syncing) the gatePos is also far enough along that we also want to switch a note on?
		if (!syncedNow && gatePos >= 16777216) {
			switchNoteOn(settings, instruction);
		}
	}

	if (!syncedNow) gatePos &= 16777215;

	gatePos += (phaseIncrement >> 8) * numSamples;
}

// Returns num ticks til we next want to come back here.
// May switch notes on and/or off.
int32_t ArpeggiatorBase::doTickForward(ArpeggiatorSettings* settings, ArpReturnInstruction* instruction,
                                       uint32_t clipCurrentPos, bool currentlyPlayingReversed) {

	// Make sure we actually intended to sync
	if (!settings->mode || !settings->syncLevel) return 2147483647;

	uint32_t ticksPerPeriod = 3 << (9 - settings->syncLevel);

	int howFarIntoPeriod = clipCurrentPos % ticksPerPeriod;

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
		if (!currentlyPlayingReversed) howFarIntoPeriod = ticksPerPeriod - howFarIntoPeriod;
	}

	return howFarIntoPeriod; // Normally we will have modified this variable above, and it no longer represents what its name says.
}

uint32_t ArpeggiatorSettings::getPhaseIncrement(int32_t arpRate) {
	uint32_t phaseIncrement;
	if (syncLevel == 0) phaseIncrement = arpRate >> 5;
	else {
		int rightShiftAmount = 9 - syncLevel; // Will be max 0
		phaseIncrement =
		    playbackHandler
		        .getTimePerInternalTickInverse(); //multiply_32x32_rshift32(playbackHandler.getTimePerInternalTickInverse(), arpRate);
		phaseIncrement >>= rightShiftAmount;
	}
	return phaseIncrement;
}
