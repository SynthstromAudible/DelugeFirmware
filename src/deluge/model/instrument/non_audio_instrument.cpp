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

#include "model/instrument/non_audio_instrument.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "processing/engines/cv_engine.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <cstring>

void NonAudioInstrument::renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos,
                                      int32_t numSamples, int32_t* reverbBuffer, int32_t reverbAmountAdjust,
                                      int32_t sideChainHitPending, bool shouldLimitDelayFeedback, bool isClipActive) {

	// MIDI / CV arpeggiator
	if (activeClip) {
		InstrumentClip* activeInstrumentClip = (InstrumentClip*)activeClip;

		if (activeInstrumentClip->arpSettings.mode != ArpMode::OFF) {
			uint32_t gateThreshold = (uint32_t)activeInstrumentClip->arpeggiatorGate + 2147483648;
			uint32_t ratchetProbability = (uint32_t)activeInstrumentClip->arpeggiatorRatchetProbability;
			uint32_t ratchetAmount = (uint32_t)activeInstrumentClip->arpeggiatorRatchetAmount;
			uint32_t sequenceLength = (uint32_t)activeInstrumentClip->arpeggiatorSequenceLength;
			uint32_t rhythm = (uint32_t)activeInstrumentClip->arpeggiatorRhythm;

			uint32_t phaseIncrement = activeInstrumentClip->arpSettings.getPhaseIncrement(
			    getFinalParameterValueExp(paramNeutralValues[deluge::modulation::params::GLOBAL_ARP_RATE],
			                              cableToExpParamShortcut(activeInstrumentClip->arpeggiatorRate)));

			ArpReturnInstruction instruction;

			arpeggiator.render(&activeInstrumentClip->arpSettings, numSamples, gateThreshold, phaseIncrement,
			                   sequenceLength, rhythm, ratchetAmount, ratchetProbability, &instruction);

			if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {
				noteOffPostArp(instruction.noteCodeOffPostArp, instruction.outputMIDIChannelOff,
				               kDefaultLiftValue); // Is there some better option than using the default lift value? The
				                                   // lift event wouldn't have occurred yet...
			}

			if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
				noteOnPostArp(instruction.noteCodeOnPostArp, instruction.arpNoteOn);
			}
		}
	}
}

void NonAudioInstrument::sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCodePreArp,
                                  int16_t const* mpeValues, int32_t fromMIDIChannel, uint8_t velocity,
                                  uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {

	ArpeggiatorSettings* arpSettings = NULL;
	if (activeClip) {
		arpSettings = &((InstrumentClip*)activeClip)->arpSettings;
	}

	ArpReturnInstruction instruction;

	// Note on
	if (isOn) {

		// Run everything by the Arp...
		arpeggiator.noteOn(arpSettings, noteCodePreArp, velocity, &instruction, fromMIDIChannel, mpeValues);

		if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
			noteOnPostArp(instruction.noteCodeOnPostArp, instruction.arpNoteOn);
		}
	}

	// Note off
	else {

		// Run everything by the Arp...
		arpeggiator.noteOff(arpSettings, noteCodePreArp, &instruction);

		if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {
			noteOffPostArp(instruction.noteCodeOffPostArp, instruction.outputMIDIChannelOff, velocity);
		}
		if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE && type == OutputType::CV) {
			noteOnPostArp(instruction.noteCodeOnPostArp, instruction.arpNoteOn);
		}
	}
}

// Inherit / overrides from both MelodicInstrument and ModControllable
void NonAudioInstrument::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t whichExpressionDimension,
                                                                  int32_t channelOrNoteNumber,
                                                                  MIDICharacteristic whichCharacteristic) {
	ArpeggiatorSettings* settings = getArpSettings();

	int32_t n;
	int32_t nEnd;

	// If for note, we can search right to it.
	if (whichCharacteristic == MIDICharacteristic::NOTE) {
		n = arpeggiator.notes.search(channelOrNoteNumber, GREATER_OR_EQUAL);
		if (n < arpeggiator.notes.getNumElements()) {
			nEnd = 0;
			goto lookAtArpNote;
		}
		return;
	}

	nEnd = arpeggiator.notes.getNumElements();

	for (n = 0; n < nEnd; n++) {
lookAtArpNote:
		ArpNote* arpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[util::to_underlying(whichCharacteristic)] == channelOrNoteNumber) {

			// Update the MPE value in the ArpNote. If arpeggiating, it'll get read from there the next time there's a
			// note-on-post-arp. I realise this is potentially frequent writing when it's only going to be read
			// occasionally, but since we're already this far (the Instrument being notified), it's hardly any extra
			// work.
			arpNote->mpeValues[whichExpressionDimension] = newValue >> 16;

			int32_t noteCodeBeforeArpeggiation =
			    arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];
			int32_t noteCodeAfterArpeggiation = noteCodeBeforeArpeggiation;

			// If there's actual arpeggiation happening right now and noteMode is not AS_PLAYED...
			if ((settings != nullptr) && settings->mode != ArpMode::OFF
			    && settings->noteMode != ArpNoteMode::AS_PLAYED) {
				// If it's not this noteCode's turn, then do nothing with it
				if (arpeggiator.whichNoteCurrentlyOnPostArp != n) {
					continue;
				}

				// Otherwise, just take note of which octave is currently outputting
				noteCodeAfterArpeggiation += arpeggiator.currentOctave;

				// We'll send even if the gate isn't still active. Seems the most sensible. And the release might still
				// be sounding on the connected synth, so this probably makes sense
			}

			// Send this even if arp is on and this note isn't currently sounding: its release might still be
			polyphonicExpressionEventPostArpeggiator(newValue, noteCodeAfterArpeggiation, whichExpressionDimension,
			                                         arpNote);
		}
	}
	// Traverse also notesAsPlayed so those get updated mpeValues too, in case noteMode is changed to AsPlayed
	for (n = 0; n < arpeggiator.notesAsPlayed.getNumElements(); n++) {
		ArpNote* arpNote = (ArpNote*)arpeggiator.notesAsPlayed.getElementAddress(n);
		if (arpNote->inputCharacteristics[util::to_underlying(whichCharacteristic)] == channelOrNoteNumber) {

			// Update the MPE value in the ArpNote. If arpeggiating, it'll get read from there the next time there's a
			// note-on-post-arp. I realise this is potentially frequent writing when it's only going to be read
			// occasionally, but since we're already this far (the Instrument being notified), it's hardly any extra
			// work.
			arpNote->mpeValues[whichExpressionDimension] = newValue >> 16;

			int32_t noteCodeBeforeArpeggiation =
			    arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)];
			int32_t noteCodeAfterArpeggiation = noteCodeBeforeArpeggiation;

			// If there's actual arpeggiation happening right now and noteMode is AS_PLAYED...
			if ((settings != nullptr) && settings->mode != ArpMode::OFF
			    && settings->noteMode == ArpNoteMode::AS_PLAYED) {
				// If it's not this noteCode's turn, then do nothing with it
				if (arpeggiator.whichNoteCurrentlyOnPostArp != n) {
					continue;
				}

				// Otherwise, just take note of which octave is currently outputting
				noteCodeAfterArpeggiation += arpeggiator.currentOctave;

				// We'll send even if the gate isn't still active. Seems the most sensible. And the release might still
				// be sounding on the connected synth, so this probably makes sense
			}
		}
	}
}

// Returns num ticks til next arp event
int32_t NonAudioInstrument::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	if (!activeClip) {
		return 2147483647;
	}

	InstrumentClip* activeInstrumentClip = (InstrumentClip*)activeClip;
	if (activeInstrumentClip->arpSettings.mode != ArpMode::OFF) {
		uint32_t sequenceLength = (uint32_t)activeInstrumentClip->arpeggiatorSequenceLength;
		uint32_t rhythm = (uint32_t)activeInstrumentClip->arpeggiatorRhythm;
		uint32_t ratchetAmount = (uint32_t)activeInstrumentClip->arpeggiatorRatchetAmount;
		uint32_t ratchetProbability = (uint32_t)activeInstrumentClip->arpeggiatorRatchetProbability;
		arpeggiator.updateParams(sequenceLength, rhythm, ratchetAmount, ratchetProbability);
	}

	ArpReturnInstruction instruction;

	int32_t ticksTilNextArpEvent = arpeggiator.doTickForward(&((InstrumentClip*)activeClip)->arpSettings, &instruction,
	                                                         currentPos, activeClip->currentlyPlayingReversed);

	if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {
		noteOffPostArp(instruction.noteCodeOffPostArp, instruction.outputMIDIChannelOff,
		               kDefaultLiftValue); // Is there some better option than using the default lift value? The lift
		                                   // event wouldn't have occurred yet...
	}

	if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
		noteOnPostArp(instruction.noteCodeOnPostArp, instruction.arpNoteOn);
	}

	return ticksTilNextArpEvent;
}

// Unlike other Outputs, these don't have ParamManagers backed up at the Song level
ParamManager* NonAudioInstrument::getParamManager(Song* song) {

	if (activeClip) {
		return &activeClip->paramManager;
	}
	else {
		return NULL;
	}
}

bool NonAudioInstrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	char const* slotXMLTag = getSlotXMLTag();

	if (!strcmp(tagName, slotXMLTag)) {
		channel = reader.readTagOrAttributeValueInt();
	}

	else {
		return MelodicInstrument::readTagFromFile(reader, tagName);
	}

	reader.exitTag();
	return true;
}

bool NonAudioInstrument::needsEarlyPlayback() const {
	return (type == OutputType::MIDI_OUT && channel == MIDI_CHANNEL_TRANSPOSE);
}
