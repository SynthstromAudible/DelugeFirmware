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
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/oled.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/drum/non_audio_drum.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param_collection.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <string.h>

MIDIDrum::MIDIDrum() : NonAudioDrum(DrumType::MIDI) {
	channel = 0;
	note = 0;
	modKnobCCAssignments.fill(CC_NUMBER_NONE);
}

void MIDIDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, int16_t const* mpeValues,
                      int32_t fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {

	// Send MIDI note on using routing data class
	uint32_t deviceFilter = outputRouting.toDeviceFilter();
	midiEngine.sendMidi(this, MIDIMessage::noteOn(channel, note, velocity), kMIDIOutputFilterNoMPE, true, deviceFilter);
}

void MIDIDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity) {

	// Send MIDI note off using routing data class
	uint32_t deviceFilter = outputRouting.toDeviceFilter();
	midiEngine.sendMidi(this, MIDIMessage::noteOff(channel, note, velocity), kMIDIOutputFilterNoMPE, true,
	                    deviceFilter);
}

void MIDIDrum::noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) {
	NonAudioDrum::noteOnPostArp(noteCodePostArp, arpNote, noteIndex);
}

void MIDIDrum::noteOffPostArp(int32_t noteCodePostArp) {
	NonAudioDrum::noteOffPostArp(noteCodePostArp);
}

void MIDIDrum::writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) {
	writer.writeOpeningTagBeginning("midiOutput", true);
	writer.writeAttribute("channel", channel, false);
	writer.writeAttribute("note", note, false);
	writer.writeAttribute("outputDevice", outputRouting.device, false);
	writer.writeOpeningTagEnd();

	NonAudioDrum::writeArpeggiatorToFile(writer);

	if (savingSong) {
		Drum::writeMIDICommandsToFile(writer);

		// Write ccLabels section directly (MIDI drums in kits don't use midiDevice wrapper)
		writeCCLabelsToFile(writer);
	}

	writer.writeClosingTag("midiOutput", true, true);
}

Error MIDIDrum::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "outputDevice")) {
			// Try reading the value first, then exitTag
			int32_t deviceValue = reader.readTagOrAttributeValueInt();
			reader.exitTag("outputDevice");
			outputRouting.device = (uint8_t)deviceValue;
		}
		else if (!strcmp(tagName, "channel")) {
			channel = reader.readTagOrAttributeValueInt();
			reader.exitTag("channel");
		}
		else if (!strcmp(tagName, "note")) {
			note = reader.readTagOrAttributeValueInt();
			reader.exitTag("note");
		}
		else if (!strcmp(tagName, "modKnobs")) {
			// Handle modKnobs if present (for backward compatibility with old debug format)
			readModKnobAssignmentsFromFile(reader);
			reader.exitTag("modKnobs");
		}
		else if (!strcmp(tagName, "ccLabels")) {
			// Handle ccLabels section (for kit row automation)
			readCCLabelsFromFile(reader);
			reader.exitTag("ccLabels");
		}
		else if (NonAudioDrum::readDrumTagFromFile(reader, tagName)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	return Error::NONE;
}

void MIDIDrum::getName(char* buffer) {
	strcpy(buffer, "MIDI");
}

void MIDIDrum::killAllVoices() {
	NonAudioDrum::killAllVoices();
}

int8_t MIDIDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset, uint8_t whichModEncoder) {
	NonAudioDrum::modEncoderAction(modelStack, offset, whichModEncoder);

	if ((getCurrentUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) && (currentUIMode == UI_MODE_AUDITIONING)) {
		if (whichModEncoder == 1) {
			modChange(modelStack, offset, &noteEncoderCurrentOffset, &note, 128);
		}
	}

	return -64;
}

void MIDIDrum::expressionEvent(int32_t newValue, int32_t expressionDimension) {
	NonAudioDrum::expressionEvent(newValue, expressionDimension);
}

void MIDIDrum::polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
                                                        int32_t channelOrNoteNumber,
                                                        MIDICharacteristic whichCharacteristic) {
	NonAudioDrum::polyphonicExpressionEventOnChannelOrNote(newValue, expressionDimension, channelOrNoteNumber,
	                                                       whichCharacteristic);
}

// MIDI CC automation support (similar to MIDIInstrument)
void MIDIDrum::ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value, ModelStackWithTimelineCounter* modelStack) {
	int32_t valueBig = (value - 64) << 25;
	processParamFromInputMIDIChannel(cc, valueBig, modelStack);
}

void MIDIDrum::processParamFromInputMIDIChannel(int32_t cc, int32_t newValue,
                                                ModelStackWithTimelineCounter* modelStack) {
	int32_t modPos = 0;
	int32_t modLength = 0;

	if (modelStack->timelineCounterIsSet()) {
		modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);

		// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
		// region.
		if (view.modLength
		    && modelStack->getTimelineCounter() == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
			modPos = view.modPos;
			modLength = view.modLength;
		}
	}

	// For MIDI drums in kits, we need to get the NoteRow's ParamManager
	// The NoteRow's ParamManager is where MIDI CC automation is stored
	// Get the InstrumentClip and find the NoteRow for this drum
	InstrumentClip* instrumentClip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull();
	if (!instrumentClip) {
		// If no active clip, we can't record automation
		sendCC(cc, newValue);
		return;
	}

	// Get the NoteRow for this drum
	NoteRow* noteRow = instrumentClip->getNoteRowForDrum(this);
	if (!noteRow) {
		// If no NoteRow for this drum, we can't record automation
		sendCC(cc, newValue);
		return;
	}

	// Get the NoteRow's ParamManager for MIDI drums
	ParamManager* paramManager = &noteRow->paramManager;

	// Create ModelStack with NoteRow and ParamManager
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, noteRow);
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStackWithNoteRow->addOtherTwoThings(toModControllable(), paramManager);

	ModelStackWithAutoParam* modelStackWithAutoParam =
	    getParamToControlFromInputMIDIChannel(cc, modelStackWithThreeMainThings);

	if (modelStackWithAutoParam && modelStackWithAutoParam->autoParam) {
		modelStackWithAutoParam->autoParam->setValuePossiblyForRegion(
		    newValue, modelStackWithAutoParam, modPos, modLength,
		    false); // Don't delete nodes in linear run, cos this might need to be outputted as MIDI again
	}

	// Also send the CC value for immediate output
	sendCC(cc, newValue);
}

ModelStackWithAutoParam* MIDIDrum::getParamToControlFromInputMIDIChannel(int32_t cc,
                                                                         ModelStackWithThreeMainThings* modelStack) {
	if (!modelStack->paramManager) { // Could be NULL - if the user is holding down an audition pad in Arranger, and we
		                             // have no Clips
noParam:
		return modelStack->addParamCollectionAndId(nullptr, nullptr, 0)->addAutoParam(nullptr); // "No param"
	}

	ParamCollectionSummary* summary;
	int32_t paramId = cc;

	switch (cc) {
	case CC_NUMBER_PITCH_BEND:
		paramId = 0;
		goto expressionParam;
	case CC_NUMBER_Y_AXIS:
		paramId = 1;
		goto expressionParam;

	case CC_NUMBER_AFTERTOUCH:
		paramId = 2;
expressionParam:
		modelStack->paramManager->ensureExpressionParamSetExists(); // Allowed to fail
		summary = modelStack->paramManager->getExpressionParamSetSummary();
		if (!summary->paramCollection) {
			goto noParam;
		}
		break;

	case CC_NUMBER_NONE:
		goto noParam;

	default:
		// Ensure MIDI parameter collection exists for kit rows
		if (!modelStack->paramManager->containsAnyMainParamCollections()) {
			Error error = modelStack->paramManager->setupMIDI();
			if (error != Error::NONE) {
				goto noParam;
			}
		}
		summary = modelStack->paramManager->getMIDIParamCollectionSummary();
		break;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	return summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId,
	    true); // Yes we do want to force creating it even if we're not recording - so the level indicator can update
	           // for the user
}

int32_t MIDIDrum::changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode) {
	int8_t* cc = &modKnobCCAssignments[modKnobMode * kNumPhysicalModKnobs + whichModEncoder];

	int32_t newCC = *cc;

	newCC += offset;
	if (newCC < 0) {
		newCC += kNumCCNumbersIncludingFake;
	}
	else if (newCC >= kNumCCNumbersIncludingFake) {
		newCC -= kNumCCNumbersIncludingFake;
	}
	if (newCC == 1) {
		// mod wheel is actually CC_NUMBER_Y_AXIS (122) internally
		newCC += offset;
	}

	*cc = newCC;

	return newCC;
}

int32_t MIDIDrum::getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt,
                                   int32_t stopAt) {
	int32_t cc = startAt;
	while (cc != stopAt) {
		bool found = false;
		for (int32_t i = 0; i < kNumModButtons * kNumPhysicalModKnobs; i++) {
			if (modKnobCCAssignments[i] == cc) {
				found = true;
				break;
			}
		}
		if (!found) {
			return cc;
		}
		cc += direction;
		if (cc < 0) {
			cc = kNumCCNumbersIncludingFake - 1;
		}
		else if (cc >= kNumCCNumbersIncludingFake) {
			cc = 0;
		}
	}
	return -1;
}

Error MIDIDrum::moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithThreeMainThings* modelStack) {
	ModelStackWithAutoParam* modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(oldCC, modelStack);

	AutoParam* oldParam = modelStackWithAutoParam->autoParam;
	if (!oldParam) {
		return Error::NONE;
	}

	AutoParamState state;
	oldParam->swapState(&state, modelStackWithAutoParam);

	// Delete or clear old parameter
	MIDIParamCollection* midiParamCollection = modelStackWithAutoParam->paramManager->getMIDIParamCollection();

	// CC (besides 74)
	if (modelStackWithAutoParam->paramCollection == (ParamCollection*)midiParamCollection) {
		midiParamCollection->params.deleteAtKey(oldCC);
	}

	// Expression param
	else {
		((ExpressionParamSet*)modelStackWithAutoParam->paramCollection)
		    ->params[modelStackWithAutoParam->paramId]
		    .setCurrentValueBasicForSetup(0);
	}

	modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(newCC, modelStack);
	AutoParam* newParam = modelStackWithAutoParam->autoParam;
	if (!newParam) {
		return Error::INSUFFICIENT_RAM;
	}

	newParam->swapState(&state, modelStackWithAutoParam);

	return Error::NONE;
}

int32_t MIDIDrum::moveAutomationToDifferentCC(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode,
                                              ModelStackWithThreeMainThings* modelStack) {
	int8_t* cc = &modKnobCCAssignments[modKnobMode * kNumPhysicalModKnobs + whichModEncoder];
	if (*cc >= CC_NUMBER_NONE) {
		return *cc;
	}

	int32_t newCC = *cc + offset;
	if (newCC < 0) {
		newCC += CC_NUMBER_NONE;
	}
	else if (newCC >= CC_NUMBER_NONE) {
		newCC -= CC_NUMBER_NONE;
	}

	Error error = moveAutomationToDifferentCC(*cc, newCC, modelStack);
	if (error != Error::NONE) {
		return -1;
	}

	*cc = newCC;
	return newCC;
}

bool MIDIDrum::doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int32_t cc) {
	bool automationExists = false;

	ModelStackWithAutoParam* modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(cc, modelStack);
	if (modelStackWithAutoParam->autoParam) {
		automationExists = modelStackWithAutoParam->autoParam->isAutomated();
	}

	return automationExists;
}

Error MIDIDrum::readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos, ParamManagerForTimeline* paramManager) {
	return Error::NONE; // Placeholder, actual reading is in readFromFile
}

// ModControllable implementation
bool MIDIDrum::modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) {
	if (on) {
		if (currentUIMode == UI_MODE_NONE) {
			if (getCurrentUI()->toClipMinder()) {
				currentUIMode = UI_MODE_SELECTING_MIDI_CC;

				int32_t cc = modKnobCCAssignments[modKnobMode * kNumPhysicalModKnobs + whichModEncoder];

				bool automationExists = doesAutomationExistOnMIDIParam(modelStack, cc);
				InstrumentClipMinder::editingMIDICCForWhichModKnob = whichModEncoder;
				InstrumentClipMinder::drawMIDIControlNumber(cc, automationExists);
				return true;
			}
			else {
				return false;
			}
		}
		else {
			return false;
		}
	}

	// De-press
	else {
		if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
			currentUIMode = UI_MODE_NONE;
			if (display->haveOLED()) {
				deluge::hid::display::OLED::removePopup();
			}
			else {
				InstrumentClipMinder::redrawNumericDisplay();
			}
		}
		return false;
	}
}

void MIDIDrum::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	// If we're leaving this mod function or anything else is happening, we want to be sure that stutter has stopped
	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		currentUIMode = UI_MODE_NONE;
		if (display->haveOLED()) {
			deluge::hid::display::OLED::removePopup();
		}
		else {
			InstrumentClipMinder::redrawNumericDisplay();
		}
	}
}

ModelStackWithAutoParam* MIDIDrum::getParamFromModEncoder(int32_t whichModEncoder,
                                                          ModelStackWithThreeMainThings* modelStack,
                                                          bool allowCreation) {
	int32_t cc = modKnobCCAssignments[modKnobMode * kNumPhysicalModKnobs + whichModEncoder];
	return getParamToControlFromInputMIDIChannel(cc, modelStack);
}

int32_t MIDIDrum::getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) {
	return 0; // Default position for non-existent params
}

bool MIDIDrum::valueChangedEnoughToMatter(int32_t old_value, int32_t new_value, deluge::modulation::params::Kind kind,
                                          uint32_t paramID) {
	if (kind == deluge::modulation::params::Kind::EXPRESSION) {
		if (paramID == X_PITCH_BEND) {
			// pitch is in 14 bit instead of 7
			return old_value >> 18 != new_value >> 18;
		}
		// aftertouch and mod wheel are positive only and recorded into a smaller range than CCs
		return old_value >> 24 != new_value >> 24;
	}
	return old_value >> 25 != new_value >> 25;
}

// CC sending method
void MIDIDrum::sendCC(int32_t cc, int32_t value) {

	// Convert automation value to MIDI CC value (same as MIDIParamCollection::autoparamValueToCC)
	int32_t rShift = 25;
	int32_t roundingAmountToAdd = 1 << (rShift - 1);
	int32_t maxValue = 2147483647 - roundingAmountToAdd;

	if (value > maxValue) {
		value = maxValue;
	}
	int32_t ccValue = (value + roundingAmountToAdd) >> rShift;

	uint32_t deviceFilter = outputRouting.toDeviceFilter();
	midiEngine.sendMidi(this, MIDIMessage::cc(getOutputMasterChannel(), cc, ccValue + 64), kMIDIOutputFilterNoMPE, true,
	                    deviceFilter);
}

// Get the master channel for output (handles MPE zones like MIDI instruments)
int32_t MIDIDrum::getOutputMasterChannel() {
	switch (channel) {
	case MIDI_CHANNEL_MPE_LOWER_ZONE:
		return 0;

	case MIDI_CHANNEL_MPE_UPPER_ZONE:
		return 15;

	default:
		return channel;
	}
}

// CC label file I/O methods (following MIDIInstrument pattern exactly)
void MIDIDrum::writeCCLabelsToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("ccLabels");
	for (int32_t i = 0; i < kNumRealCCNumbers; i++) {
		if (i != CC_EXTERNAL_MOD_WHEEL) {
			auto it = labels.find(i);
			char ccNumber[10];
			intToString(i, ccNumber, 1);
			if (it != labels.end()) {
				writer.writeAttribute(ccNumber, it->second.data());
			}
			else {
				writer.writeAttribute(ccNumber, "");
			}
		}
	}
	writer.closeTag();
}

Error MIDIDrum::readModKnobAssignmentsFromFile(Deserializer& reader) {
	int32_t m = 0;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "modKnob")) {
			// Read the modKnob attributes directly (not nested elements)
			char const* contents = reader.readTagOrAttributeValue();
			int32_t cc = CC_NUMBER_NONE;

			if (!strcasecmp(contents, "bend")) {
				cc = CC_NUMBER_PITCH_BEND;
			}
			else if (!strcasecmp(contents, "aftertouch")) {
				cc = CC_NUMBER_AFTERTOUCH;
			}
			else if (!strcasecmp(contents, "none")) {
				cc = CC_NUMBER_NONE;
			}
			else {
				cc = stringToInt(contents);
			}

			modKnobCCAssignments[m] = cc;
			m++;
			reader.exitTag("modKnob");
		}
		else {
			reader.exitTag(tagName);
		}
		if (m >= kNumModButtons * kNumPhysicalModKnobs) {
			break;
		}
	}
	return Error::NONE;
}

Error MIDIDrum::readCCLabelsFromFile(Deserializer& reader) {
	Error error = Error::FILE_UNREADABLE;

	int32_t cc = 0;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		char ccNumber[10];
		cc = stringToInt(tagName);

		if (cc < 0 || cc >= kNumRealCCNumbers) {
			reader.exitTag();
			continue;
		}

		labels[cc] = reader.readTagOrAttributeValue();

		error = Error::NONE;

		reader.exitTag();
	}

	return error;
}
