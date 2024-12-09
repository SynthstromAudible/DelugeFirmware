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

	arpeggiatorRate = 0;
	arpeggiatorNoteProbability = 4294967295u; // Default to 50 if not set in XML
	arpeggiatorRatchetProbability = 0;
	arpeggiatorRatchetAmount = 0;
	arpeggiatorSequenceLength = 0;
	arpeggiatorRhythm = 0;
	arpeggiatorGate = 0;
	arpeggiatorSpreadVelocity = 0;
	arpeggiatorSpreadGate = 0;
	arpeggiatorSpreadOctave = 0;
}

bool NonAudioDrum::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	return true;
}

void NonAudioDrum::unassignAllVoices() {
	if (hasAnyVoices()) {
		noteOff(NULL);
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
		noteOff(NULL);
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
		noteOn(modelStack, lastVelocity, NULL, zeroMPEValues);
	}
}

void NonAudioDrum::writeArpeggiatorToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("arpeggiator");
	writer.writeAttribute("mode", (char*)arpModeToString(arpSettings.mode));
	writer.writeAttribute("syncLevel", arpSettings.syncLevel);
	writer.writeAttribute("numOctaves", arpSettings.numOctaves);
	writer.writeAttribute("spreadLock", arpSettings.spreadLock);
	// Write locked spread params
	char buffer[9];
	// Spread velocity
	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator("lockedSpreadVelocity");
	writer.write("\"0x");
	intToHex(arpSettings.lastLockedSpreadVelocityParameterValue, buffer);
	writer.write(buffer);
	for (int i = 0; i < SPREAD_LOCK_MAX_SAVED_VALUES; i++) {
		intToHex(arpSettings.lockedSpreadVelocityValues[i], buffer, 2);
		writer.write(buffer);
	}
	writer.write("\"");
	// Spread gate
	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator("lockedSpreadGate");
	writer.write("\"0x");
	intToHex(arpSettings.lastLockedSpreadGateParameterValue, buffer);
	writer.write(buffer);
	for (int i = 0; i < SPREAD_LOCK_MAX_SAVED_VALUES; i++) {
		intToHex(arpSettings.lockedSpreadGateValues[i], buffer, 2);
		writer.write(buffer);
	}
	writer.write("\"");
	// Spread octave
	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator("lockedSpreadOctave");
	writer.write("\"0x");
	intToHex(arpSettings.lastLockedSpreadOctaveParameterValue, buffer);
	writer.write(buffer);
	for (int i = 0; i < SPREAD_LOCK_MAX_SAVED_VALUES; i++) {
		intToHex(arpSettings.lockedSpreadOctaveValues[i], buffer, 2);
		writer.write(buffer);
	}
	writer.write("\"");

	writer.writeAttribute("gate", arpeggiatorGate);
	writer.writeAttribute("rate", arpeggiatorRate);
	writer.writeAttribute("noteProbability", arpeggiatorNoteProbability);
	writer.writeAttribute("ratchetProbability", arpeggiatorRatchetProbability);
	writer.writeAttribute("ratchetAmount", arpeggiatorRatchetAmount);
	writer.writeAttribute("sequenceLength", arpeggiatorSequenceLength);
	writer.writeAttribute("rhythm", arpeggiatorRhythm);
	writer.writeAttribute("spreadVelocity", arpeggiatorSpreadVelocity);
	writer.writeAttribute("spreadGate", arpeggiatorSpreadGate);
	writer.writeAttribute("spreadOctave", arpeggiatorSpreadOctave);
	writer.writeAttribute("syncType", arpSettings.syncType);
	writer.writeAttribute("arpMode", (char*)arpModeToString(arpSettings.mode));
	writer.writeAttribute("chordType", arpSettings.chordTypeIndex);
	writer.writeAttribute("noteMode", (char*)arpNoteModeToString(arpSettings.noteMode));
	writer.writeAttribute("octaveMode", (char*)arpOctaveModeToString(arpSettings.octaveMode));
	writer.writeAttribute("mpeVelocity", (char*)arpMpeModSourceToString(arpSettings.mpeVelocity));

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
				arpeggiatorRate = reader.readTagOrAttributeValueInt();
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "noteProbability")) {
				arpeggiatorNoteProbability = reader.readTagOrAttributeValueInt();
				reader.exitTag("noteProbability");
			}
			else if (!strcmp(tagName, "ratchetProbability")) {
				arpeggiatorRatchetProbability = reader.readTagOrAttributeValueInt();
				reader.exitTag("ratchetProbability");
			}
			else if (!strcmp(tagName, "ratchetAmount")) {
				arpeggiatorRatchetAmount = reader.readTagOrAttributeValueInt();
				reader.exitTag("ratchetAmount");
			}
			else if (!strcmp(tagName, "sequenceLength")) {
				arpeggiatorSequenceLength = reader.readTagOrAttributeValueInt();
				reader.exitTag("sequenceLength");
			}
			else if (!strcmp(tagName, "rhythm")) {
				arpeggiatorRhythm = reader.readTagOrAttributeValueInt();
				reader.exitTag("rhythm");
			}
			else if (!strcmp(tagName, "spreadVelocity")) {
				arpeggiatorSpreadVelocity = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadVelocity");
			}
			else if (!strcmp(tagName, "spreadGate")) {
				arpeggiatorSpreadGate = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadGate");
			}
			else if (!strcmp(tagName, "spreadOctave")) {
				arpeggiatorSpreadOctave = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadOctave");
			}
			else if (!strcmp(tagName, "numOctaves")) {
				arpSettings.numOctaves = reader.readTagOrAttributeValueInt();
				reader.exitTag("numOctaves");
			}
			else if (!strcmp(tagName, "spreadLock")) {
				arpSettings.spreadLock = reader.readTagOrAttributeValueInt();
				reader.exitTag("spreadLock");
			}
			else if (!strcmp(tagName, "lockedSpreadVelocity")) {
				if (reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
					char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
					if (firstChars && *(uint16_t*)firstChars == charsToIntegerConstant('0', 'x')) {
						char const* hexChars =
							reader.readNextCharsOfTagOrAttributeValue(8 + 2 * SPREAD_LOCK_MAX_SAVED_VALUES);
						if (hexChars) {
							arpSettings.lastLockedSpreadVelocityParameterValue = hexToIntFixedLength(hexChars, 8);
							for (int i = 0; i < SPREAD_LOCK_MAX_SAVED_VALUES; i++) {
								arpSettings.lockedSpreadVelocityValues[i] =
									hexToIntFixedLength(&hexChars[8 + i * 2], 2);
							}
						}
					}
				}
				reader.exitTag("lockedSpreadVelocity");
			}
			else if (!strcmp(tagName, "lockedSpreadGate")) {
				if (reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
					char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
					if (firstChars && *(uint16_t*)firstChars == charsToIntegerConstant('0', 'x')) {
						char const* hexChars =
							reader.readNextCharsOfTagOrAttributeValue(8 + 2 * SPREAD_LOCK_MAX_SAVED_VALUES);
						if (hexChars) {
							arpSettings.lastLockedSpreadGateParameterValue = hexToIntFixedLength(hexChars, 8);
							for (int i = 0; i < SPREAD_LOCK_MAX_SAVED_VALUES; i++) {
								arpSettings.lockedSpreadGateValues[i] =
									hexToIntFixedLength(&hexChars[8 + i * 2], 2);
							}
						}
					}
				}
				reader.exitTag("lockedSpreadGate");
			}
			else if (!strcmp(tagName, "lockedSpreadOctave")) {
				if (reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
					char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
					if (firstChars && *(uint16_t*)firstChars == charsToIntegerConstant('0', 'x')) {
						char const* hexChars =
							reader.readNextCharsOfTagOrAttributeValue(8 + 2 * SPREAD_LOCK_MAX_SAVED_VALUES);
						if (hexChars) {
							arpSettings.lastLockedSpreadOctaveParameterValue = hexToIntFixedLength(hexChars, 8);
							for (int i = 0; i < SPREAD_LOCK_MAX_SAVED_VALUES; i++) {
								arpSettings.lockedSpreadOctaveValues[i] =
									hexToIntFixedLength(&hexChars[8 + i * 2], 2);
							}
						}
					}
				}
				reader.exitTag("lockedSpreadOctave");
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
				arpeggiatorGate = reader.readTagOrAttributeValueInt();
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
