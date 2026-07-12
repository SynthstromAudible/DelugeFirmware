/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/drum/midi_drum.h"
#include "deluge-project/doc_drum.h"
#include "foundation/panic.h" // FREEZE_WITH_ERROR (M3 owner-3 unbound-read guard)
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "io/midi/midi_engine.h"
#include "model/drum/non_audio_drum.h"
#include "storage/storage_manager.h"
#include "util/string.h"

#include <string.h>

// The channel (0) and note (0) defaults come from DocDrum's member initializers, applied when adoptDrum binds the
// record (the ctor runs before the drum is bound, so it can't write the model here).
MIDIDrum::MIDIDrum() : NonAudioDrum(DrumType::MIDI) {
}

// M3 owner-3: read the note from the authoritative DOM record (the drum is bound before any read — adopt precedes tag
// reads at load; runtime drums bind on addDrum). The ALPHA guard makes any stray unbound read fail loudly.
uint8_t MIDIDrum::note() const {
	if (ALPHA_OR_BETA_VERSION && docDrum_ == nullptr) {
		FREEZE_WITH_ERROR("Dr3T");
	}
	return docDrum_->note;
}

// M3 owner-3: the write funnel for the note — writes the authoritative `DocDrum::note` (the drum is bound before any
// write). modChange writes `docDrum_->note` in place through a raw `uint8_t*`.
void MIDIDrum::setNote(uint8_t newNote) {
	docDrum_->note = newNote;
}

void MIDIDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, int16_t const* mpeValues,
                      int32_t fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	ArpeggiatorSettings* arpSettings = getArpSettings();
	ArpReturnInstruction instruction;
	// Run everything by the Arp...
	arpeggiator.noteOn(arpSettings, note(), velocity, &instruction, fromMIDIChannel, mpeValues);
	if (instruction.arpNoteOn != nullptr) {
		for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.arpNoteOn->noteCodeOnPostArp[n] == ARP_NOTE_NONE) {
				break;
			}
			noteOnPostArp(instruction.arpNoteOn->noteCodeOnPostArp[n], instruction.arpNoteOn, n);
			instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;
		}
	}
}

void MIDIDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity) {
	ArpeggiatorSettings* arpSettings = getArpSettings();
	ArpReturnInstruction instruction;
	// Run everything by the Arp...
	arpeggiator.noteOff(arpSettings, note(), &instruction);
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.glideNoteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		noteOffPostArp(instruction.glideNoteCodeOffPostArp[n]);
	}
	for (int32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
		if (instruction.noteCodeOffPostArp[n] == ARP_NOTE_NONE) {
			break;
		}
		noteOffPostArp(instruction.noteCodeOffPostArp[n]);
	}
}

void MIDIDrum::noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) {
	NonAudioDrum::noteOnPostArp(noteCodePostArp, arpNote, noteIndex);
	lastVelocity = arpNote->velocity;
	midiEngine.sendNote(this, true, noteCodePostArp, arpNote->velocity, channel(), kMIDIOutputFilterNoMPE);
}

void MIDIDrum::noteOffPostArp(int32_t noteCodePostArp) {
	NonAudioDrum::noteOffPostArp(noteCodePostArp);
	midiEngine.sendNote(this, false, noteCodePostArp, kDefaultNoteOffVelocity, channel(), kMIDIOutputFilterNoMPE);
}

void MIDIDrum::killAllVoices() {
	if (hasActiveVoices()) {
		noteOff(nullptr);
	}
	arpeggiator.reset();
}

void MIDIDrum::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) {
	writer.writeOpeningTagBeginning("midiOutput", true);
	writeDrumTagsToFile(writer, savingSong);
	writer.writeAttribute("channel", channel(), false);
	writer.writeAttribute("note", note(), false);
	writer.writeOpeningTagEnd();

	NonAudioDrum::writeArpeggiatorToFile(writer);

	if (savingSong) {
		Drum::writeMIDICommandsToFile(writer);
	}
	writer.writeClosingTag("midiOutput", true, true);
}

Error MIDIDrum::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "note")) {
			setNote(reader.readTagOrAttributeValueInt());
			reader.exitTag("note");
		}
		else if (NonAudioDrum::readDrumTagFromFile(reader, tagName)) {}
		else {
			reader.exitTag(tagName);
		}
	}

	return Error::NONE;
}

std::string MIDIDrum::getDrumName() {
	int32_t channelToDisplay = channel() + 1;
	std::string buffer;

	if (display->haveOLED()) {
		if (!name().empty()) {
			buffer.append(name());
			buffer.append("\n");
		}
		buffer.append("MIDI CH");
		buffer.append(deluge::string::fromInt(channelToDisplay));
		buffer.append(": ");
		buffer.append(deluge::string::fromInt(note()));
		buffer.append(" (");
		char noteLabel[5];
		noteCodeToString(note(), noteLabel);
		buffer.append(noteLabel);
		buffer.append(")");
	}
	else {

		// Old 7-seg logic adapted to std::string.
		if (channelToDisplay < 10 && note() < 100) {
			buffer = " ";
		}

		// If can't fit everything on display, show channel as hexadecimal
		if (channelToDisplay >= 10 && note() >= 100) {
			buffer.push_back('A' + channelToDisplay - 10);
		}
		else {
			buffer.append(deluge::string::fromInt(channelToDisplay));
		}

		buffer.push_back('.');

		if (note() < 10 && channelToDisplay < 100) {
			buffer.push_back(' ');
		}
		buffer.append(deluge::string::fromInt(note()));
	}

	return buffer;
}

int8_t MIDIDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset, uint8_t whichModEncoder) {

	NonAudioDrum::modEncoderAction(modelStack, offset, whichModEncoder);

	if ((getCurrentUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) && (currentUIMode == UI_MODE_AUDITIONING)) {
		if (whichModEncoder == 1) {
			// M3 owner-3: modChange writes the authoritative DOM scalar in place (`value` points at docDrum_->note).
			modChange(modelStack, offset, &noteEncoderCurrentOffset, &docDrum_->note, 128);
		}
	}

	return -64;
}

void MIDIDrum::expressionEvent(int32_t newValue, int32_t expressionDimension) {

	// Aftertouch only
	if (expressionDimension == 2) {
		int32_t value7 = newValue >> 24;
		// Note: use the note code currently on post-arp, because this drum supports "Chord Simulator" and "Octaves" and
		// the note code could be different
		midiEngine.sendPolyphonicAftertouch(this, channel(), value7, arpeggiator.active_note.noteCodeOnPostArp[0],
		                                    kMIDIOutputFilterNoMPE);
	}
}

void MIDIDrum::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
                                                        int32_t channelOrNoteNumber,
                                                        MIDICharacteristic whichCharacteristic) {
	// Because this is a Drum, we disregard the noteCode (which is what channelOrNoteNumber always is in our case - but
	// yeah, that's all irrelevant.
	expressionEvent(newValue, expressionDimension);
}
