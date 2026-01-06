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
#include "dsp/stereo_sample.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "processing/engines/cv_engine.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <cstring>

void NonAudioInstrument::renderOutput(ModelStack* modelStack, std::span<StereoSample> output, int32_t* reverbBuffer,
                                      int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                      bool shouldLimitDelayFeedback, bool isClipActive) {
	// MIDI / CV arpeggiator
	if (activeClip) {
		InstrumentClip* activeInstrumentClip = (InstrumentClip*)activeClip;

		if (activeInstrumentClip->arpSettings.mode != ArpMode::OFF) {
			uint32_t gateThreshold = (uint32_t)activeInstrumentClip->arpSettings.gate + 2147483648;
			uint32_t phaseIncrement = activeInstrumentClip->arpSettings.getPhaseIncrement(
			    getFinalParameterValueExp(paramNeutralValues[deluge::modulation::params::GLOBAL_ARP_RATE],
			                              cableToExpParamShortcut(activeInstrumentClip->arpSettings.rate)));

			ArpReturnInstruction instruction;

			arpeggiator.render(&activeInstrumentClip->arpSettings, &instruction, output.size(), gateThreshold,
			                   phaseIncrement);

			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
					break;
				}
				noteOffPostArp(instruction.glideNoteCodeOffPostArp[n], instruction.glideOutputMIDIChannelOff[n],
				               kDefaultLiftValue, n); // Is there some better option than using the default lift
				                                      // value? The lift event wouldn't have occurred yet...
			}
			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
					break;
				}
				noteOffPostArp(instruction.noteCodeOffPostArp[n], instruction.outputMIDIChannelOff[n],
				               kDefaultLiftValue, n); // Is there some better option than using the default lift
				                                      // value? The lift event wouldn't have occurred yet...
			}
			if (instruction.arpNoteOn != nullptr) {
				for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
					if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
						break;
					}
					instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;
					noteOnPostArp(instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn, n);
				}
			}
		}
	}
}

void NonAudioInstrument::sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCodePreArp,
                                  int16_t const* mpeValues, int32_t fromMIDIChannel, uint8_t velocity,
                                  uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {

	ArpeggiatorSettings* arpSettings = nullptr;
	if (activeClip) {
		arpSettings = &((InstrumentClip*)activeClip)->arpSettings;
	}

	ArpReturnInstruction instruction;

	// Note on
	if (isOn) {

		// Run everything by the Arp...
		arpeggiator.noteOn(arpSettings, noteCodePreArp, velocity, &instruction, fromMIDIChannel, mpeValues);

		if (instruction.arpNoteOn != nullptr) {
			for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
				if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
					break;
				}
				noteOnPostArp(instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn, n);
			}
		}
	}

	// Note off
	else {

		// Run everything by the Arp...
		arpeggiator.noteOff(arpSettings, noteCodePreArp, &instruction);

		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			noteOffPostArp(instruction.glideNoteCodeOffPostArp[n], instruction.glideOutputMIDIChannelOff[n], velocity,
			               n);
		}
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			noteOffPostArp(instruction.noteCodeOffPostArp[n], instruction.outputMIDIChannelOff[n], velocity, n);
		}
		// CV instruments could switch on a note to do a glide
		if (type == OutputType::CV) {
			if (instruction.arpNoteOn != nullptr) {
				for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
					if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
						break;
					}
					noteOnPostArp(instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn, n);
				}
			}
		}
	}
}

// Inherit / overrides from both MelodicInstrument and ModControllable
void NonAudioInstrument::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
                                                                  int32_t channelOrNoteNumber,
                                                                  MIDICharacteristic whichCharacteristic) {
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
			arpNote->mpeValues[expressionDimension] = newValue >> 16;

			// Send this even if arp is on and this note isn't currently sounding: its release might still be
			for (int32_t i = 0; i < ARP_MAX_INSTRUCTION_NOTES; i++) {
				if (arpNote->noteCodeOnPostArp[i] == ARP_NOTE_NONE
				    || arpNote->outputMemberChannel[i] == MIDI_CHANNEL_NONE) {
					break;
				}
				polyphonicExpressionEventPostArpeggiator(newValue, arpNote->noteCodeOnPostArp[i], expressionDimension,
				                                         arpNote, i);
			}
		}
	}
}

// Returns num ticks til next arp event
int32_t NonAudioInstrument::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	if (!activeClip) {
		return 2147483647;
	}

	ArpReturnInstruction instruction;

	int32_t ticksTilNextArpEvent = arpeggiator.doTickForward(&((InstrumentClip*)activeClip)->arpSettings, &instruction,
	                                                         currentPos, activeClip->currentlyPlayingReversed);

	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		noteOffPostArp(instruction.glideNoteCodeOffPostArp[n], instruction.glideOutputMIDIChannelOff[n],
		               kDefaultLiftValue,
		               n); // Is there some better option than using the default lift value? The lift
		                   // event wouldn't have occurred yet...
	}
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		noteOffPostArp(instruction.noteCodeOffPostArp[n], instruction.outputMIDIChannelOff[n], kDefaultLiftValue,
		               n); // Is there some better option than using the default lift value? The lift
		                   // event wouldn't have occurred yet...
	}
	if (instruction.arpNoteOn != nullptr) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			noteOnPostArp(instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn, n);
		}
	}

	return ticksTilNextArpEvent;
}

// Unlike other Outputs, these don't have ParamManagers backed up at the Song level
ParamManager* NonAudioInstrument::getParamManager(Song* song) {

	if (activeClip) {
		return &activeClip->paramManager;
	}
	else {
		return nullptr;
	}
}

bool NonAudioInstrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	char const* slotXMLTag = getSlotXMLTag();

	if (!strcmp(tagName, slotXMLTag)) {
		setChannel(reader.readTagOrAttributeValueInt());
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
