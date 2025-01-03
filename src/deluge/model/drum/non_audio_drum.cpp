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

#include "model/drum/non_audio_drum.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

NonAudioDrum::NonAudioDrum(DrumType newType) : Drum(newType) {
	state = false;
	channelEncoderCurrentOffset = 0;
}

bool NonAudioDrum::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	return true;
}

void NonAudioDrum::unassignAllVoices() {
	if (hasAnyVoices()) {
		noteOff(nullptr);
	}
}

bool NonAudioDrum::anyNoteIsOn() {
	return state;
}

bool NonAudioDrum::hasAnyVoices() {
	return state;
}

/*
int8_t NonAudioDrum::getModKnobLevel(uint8_t whichModEncoder, ParamManagerBase* paramManager, uint32_t pos,
TimelineCounter* playPositionCounter) { if (whichModEncoder == 0) { channelEncoderCurrentOffset = 0;
    }
    return -64;
}
*/

int8_t NonAudioDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset,
                                      uint8_t whichModEncoder) {

	if ((getCurrentUI() == &instrumentClipView
	     || (getCurrentUI() == &automationView
	         && automationView.getAutomationSubType() == AutomationSubType::INSTRUMENT))
	    && currentUIMode == UI_MODE_AUDITIONING) {
		if (whichModEncoder == 0) {
			modChange(modelStack, offset, &channelEncoderCurrentOffset, &channel, getNumChannels());
		}
	}

	return -64;
}

extern int16_t zeroMPEValues[];

void NonAudioDrum::modChange(ModelStackWithThreeMainThings* modelStack, int32_t offset, int8_t* encoderOffset,
                             uint8_t* value, int32_t numValues) {

	*encoderOffset += offset;

	int32_t valueChange;
	if (*encoderOffset >= 4) {
		valueChange = 1;
	}
	else if (*encoderOffset <= -4) {
		valueChange = -1;
	}
	else {
		return;
	}

	bool wasOn = state;
	if (wasOn) {
		noteOff(nullptr);
	}

	*encoderOffset = 0;

	int32_t newValue = (int32_t)*value + valueChange;
	if (newValue < 0) {
		newValue += numValues;
	}
	else if (newValue >= numValues) {
		newValue -= numValues;
	}

	*value = newValue;

	instrumentClipView.drawDrumName(this, true);

	if (wasOn) {
		noteOn(modelStack, lastVelocity, nullptr, zeroMPEValues);
	}
}

void NonAudioDrum::writeArpeggiatorToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("arpeggiator");
	writer.writeAttribute("mode", (char*)arpModeToString(arpSettings.mode));
	writer.writeAttribute("syncLevel", arpSettings.syncLevel);
	writer.writeAttribute("numOctaves", arpSettings.numOctaves);

	writer.writeAttribute("syncType", arpSettings.syncType);
	writer.writeAttribute("arpMode", (char*)arpModeToString(arpSettings.mode));
	writer.writeAttribute("chordType", arpSettings.chordTypeIndex);
	writer.writeAttribute("noteMode", (char*)arpNoteModeToString(arpSettings.noteMode));
	writer.writeAttribute("octaveMode", (char*)arpOctaveModeToString(arpSettings.octaveMode));
	writer.writeAttribute("mpeVelocity", (char*)arpMpeModSourceToString(arpSettings.mpeVelocity));
	writer.writeAttribute("stemRepeat", arpSettings.numStepRepeats);
	writer.writeAttribute("randomizerLock", arpSettings.randomizerLock);

	writer.writeAttribute("gate", arpSettings.gate);
	writer.writeAttribute("rate", arpSettings.rate);
	writer.writeAttribute("noteProbability", arpSettings.noteProbability);
	writer.writeAttribute("bassProbability", arpSettings.bassProbability);
	writer.writeAttribute("chordProbability", arpSettings.chordProbability);
	writer.writeAttribute("ratchetProbability", arpSettings.ratchetProbability);
	writer.writeAttribute("ratchetAmount", arpSettings.ratchetAmount);
	writer.writeAttribute("sequenceLength", arpSettings.sequenceLength);
	writer.writeAttribute("chordPolyphony", arpSettings.chordPolyphony);
	writer.writeAttribute("rhythm", arpSettings.rhythm);
	writer.writeAttribute("spreadVelocity", arpSettings.spreadVelocity);
	writer.writeAttribute("spreadGate", arpSettings.spreadGate);
	writer.writeAttribute("spreadOctave", arpSettings.spreadOctave);

	// Write locked params
	// Note probability
	writer.writeAttribute("lastLockedNoteProb", arpSettings.lastLockedNoteProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedNoteProbArray", (uint8_t*)arpSettings.lockedNoteProbabilityValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);
	// Bass probability
	writer.writeAttribute("lastLockedBassProb", arpSettings.lastLockedBassProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedBassProbArray", (uint8_t*)arpSettings.lockedBassProbabilityValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);
	// Chord probability
	writer.writeAttribute("lastLockedChordProb", arpSettings.lastLockedChordProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedChordProbArray", (uint8_t*)arpSettings.lockedChordProbabilityValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);
	// Ratchet probability
	writer.writeAttribute("lastLockedRatchetProb", arpSettings.lastLockedRatchetProbabilityParameterValue);
	writer.writeAttributeHexBytes("lockedRatchetProbArray", (uint8_t*)arpSettings.lockedRatchetProbabilityValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);
	// Spread velocity
	writer.writeAttribute("lastLockedVelocitySpread", arpSettings.lastLockedSpreadVelocityParameterValue);
	writer.writeAttributeHexBytes("lockedVelocitySpreadArray", (uint8_t*)arpSettings.lockedSpreadVelocityValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);
	// Spread gate
	writer.writeAttribute("lastLockedGateSpread", arpSettings.lastLockedSpreadGateParameterValue);
	writer.writeAttributeHexBytes("lockedGateSpreadArray", (uint8_t*)arpSettings.lockedSpreadGateValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);
	// Spread octave
	writer.writeAttribute("lastLockedOctaveSpread", arpSettings.lastLockedSpreadOctaveParameterValue);
	writer.writeAttributeHexBytes("lockedOctaveSpreadArray", (uint8_t*)arpSettings.lockedSpreadOctaveValues,
	                              RANDOMIZER_LOCK_MAX_SAVED_VALUES);

	writer.closeTag();
}

bool NonAudioDrum::readDrumTagFromFile(Deserializer& reader, char const* tagName) {

	if (!strcmp(tagName, "channel")) {
		channel = reader.readTagOrAttributeValueInt();
		reader.exitTag("channel");
	}
	else if (!strcmp(tagName, "arpeggiator")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "rate")) {
				arpSettings.rate = reader.readTagOrAttributeValueInt();
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "noteProbability")) {
				arpSettings.noteProbability = reader.readTagOrAttributeValueInt();
				reader.exitTag("noteProbability");
			}
			else if (!strcmp(tagName, "bassProbability")) {
				arpSettings.bassProbability = reader.readTagOrAttributeValueInt();
				reader.exitTag("bassProbability");
			}
			else if (!strcmp(tagName, "chordProbability")) {
				arpSettings.chordProbability = reader.readTagOrAttributeValueInt();
				reader.exitTag("chordProbability");
			}
			else if (!strcmp(tagName, "ratchetProbability")) {
				arpSettings.ratchetProbability = reader.readTagOrAttributeValueInt();
				reader.exitTag("ratchetProbability");
			}
			else if (!strcmp(tagName, "ratchetAmount")) {
				arpSettings.ratchetAmount = reader.readTagOrAttributeValueInt();
				reader.exitTag("ratchetAmount");
			}
			else if (!strcmp(tagName, "sequenceLength")) {
				arpSettings.sequenceLength = reader.readTagOrAttributeValueInt();
				reader.exitTag("sequenceLength");
			}
			else if (!strcmp(tagName, "chordPolyphony")) {
				arpSettings.chordPolyphony = reader.readTagOrAttributeValueInt();
				reader.exitTag("chordPolyphony");
			}
			else if (!strcmp(tagName, "rhythm")) {
				arpSettings.rhythm = reader.readTagOrAttributeValueInt();
				reader.exitTag("rhythm");
			}
			else if (!strcmp(tagName, "spreadVelocity")) {
				arpSettings.spreadVelocity = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadVelocity");
			}
			else if (!strcmp(tagName, "spreadGate")) {
				arpSettings.spreadGate = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadGate");
			}
			else if (!strcmp(tagName, "spreadOctave")) {
				arpSettings.spreadOctave = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadOctave");
			}
			else if (!strcmp(tagName, "numOctaves")) {
				arpSettings.numOctaves = reader.readTagOrAttributeValueInt();
				reader.exitTag("numOctaves");
			}
			else if (!strcmp(tagName, "stepRepeat")) {
				arpSettings.numStepRepeats = reader.readTagOrAttributeValueInt();
				reader.exitTag("stepRepeat");
			}
			else if (!strcmp(tagName, "randomizerLock")) {
				arpSettings.randomizerLock = reader.readTagOrAttributeValueInt();
				reader.exitTag("randomizerLock");
			}
			else if (!strcmp(tagName, "lastLockedNoteProb")) {
				arpSettings.lastLockedNoteProbabilityParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedNoteProb");
			}
			else if (!strcmp(tagName, "lockedNoteProbArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedNoteProbabilityValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedNoteProbArray");
			}
			else if (!strcmp(tagName, "lastLockedBassProb")) {
				arpSettings.lastLockedBassProbabilityParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedBassProb");
			}
			else if (!strcmp(tagName, "lockedBassProbArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedBassProbabilityValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedBassProbArray");
			}
			else if (!strcmp(tagName, "lastLockedChordProb")) {
				arpSettings.lastLockedChordProbabilityParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedChordProb");
			}
			else if (!strcmp(tagName, "lockeChordProbArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedChordProbabilityValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedChordProbArray");
			}
			else if (!strcmp(tagName, "lastLockedRatchetProb")) {
				arpSettings.lastLockedRatchetProbabilityParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedRatchetProb");
			}
			else if (!strcmp(tagName, "lockeRatchetProbArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedRatchetProbabilityValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedRatchetProbArray");
			}
			else if (!strcmp(tagName, "lastLockedVelocitySpread")) {
				arpSettings.lastLockedSpreadVelocityParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedVelocitySpread");
			}
			else if (!strcmp(tagName, "lockedVelocitySpreadArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedSpreadVelocityValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedVelocitySpreadArray");
			}
			else if (!strcmp(tagName, "lastLockedGateSpread")) {
				arpSettings.lastLockedSpreadGateParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedGateSpread");
			}
			else if (!strcmp(tagName, "lockedGateSpreadArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedSpreadGateValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedGateSpreadArray");
			}
			else if (!strcmp(tagName, "lastLockedOctaveSpread")) {
				arpSettings.lastLockedSpreadOctaveParameterValue = reader.readTagOrAttributeValueInt();
				reader.exitTag("lastLockedOctaveSpread");
			}
			else if (!strcmp(tagName, "lockedOctaveSpreadArray")) {
				int len = reader.readTagOrAttributeValueHexBytes((uint8_t*)arpSettings.lockedSpreadOctaveValues,
				                                                 RANDOMIZER_LOCK_MAX_SAVED_VALUES);
				reader.exitTag("lockedOctaveSpreadArray");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				arpSettings.syncLevel = (SyncLevel)reader.readTagOrAttributeValueInt();
				reader.exitTag("syncLevel");
			}
			else if (!strcmp(tagName, "syncType")) {
				arpSettings.syncType = (SyncType)reader.readTagOrAttributeValueInt();
				reader.exitTag("syncType");
			}
			else if (!strcmp(tagName, "mode")) {
				if (song_firmware_version >= FirmwareVersion::community({1, 1, 0})) {
					reader.exitTag("mode");
				}
				else {
					// Import the old "mode" into the new splitted params "arpMode", "noteMode", and "octaveMode
					// but only if the new params are not already read and set,
					// that is, if we detect they have a value other than default
					OldArpMode oldMode = stringToOldArpMode(reader.readTagOrAttributeValue());
					if (arpSettings.mode == ArpMode::OFF && arpSettings.noteMode == ArpNoteMode::UP
					    && arpSettings.octaveMode == ArpOctaveMode::UP) {
						arpSettings.mode = oldModeToArpMode(oldMode);
						arpSettings.noteMode = oldModeToArpNoteMode(oldMode);
						arpSettings.octaveMode = oldModeToArpOctaveMode(oldMode);
						arpSettings.updatePresetFromCurrentSettings();
					}
					reader.exitTag("mode");
				}
			}
			else if (!strcmp(tagName, "arpMode")) {
				arpSettings.mode = stringToArpMode(reader.readTagOrAttributeValue());
				arpSettings.updatePresetFromCurrentSettings();
				reader.exitTag("arpMode");
			}
			else if (!strcmp(tagName, "octaveMode")) {
				arpSettings.octaveMode = stringToArpOctaveMode(reader.readTagOrAttributeValue());
				arpSettings.updatePresetFromCurrentSettings();
				reader.exitTag("octaveMode");
			}
			else if (!strcmp(tagName, "chordType")) {
				uint8_t chordTypeIndex = (uint8_t)reader.readTagOrAttributeValueInt();
				if (chordTypeIndex >= 0 && chordTypeIndex < MAX_CHORD_TYPES) {
					arpSettings.chordTypeIndex = chordTypeIndex;
				}
				reader.exitTag("chordType");
			}
			else if (!strcmp(tagName, "noteMode")) {
				arpSettings.noteMode = stringToArpNoteMode(reader.readTagOrAttributeValue());
				arpSettings.updatePresetFromCurrentSettings();
				reader.exitTag("noteMode");
			}
			else if (!strcmp(tagName, "mpeVelocity")) {
				arpSettings.mpeVelocity = stringToArpMpeModSource(reader.readTagOrAttributeValue());
				reader.exitTag("mpeVelocity");
			}
			else if (!strcmp(tagName, "gate")) {
				arpSettings.gate = reader.readTagOrAttributeValueInt();
				reader.exitTag("gate");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.match('}'); // End arpeggiator value object.
	}
	else if (Drum::readDrumTagFromFile(reader, tagName)) {}
	else {
		return false;
	}

	return true;
}
