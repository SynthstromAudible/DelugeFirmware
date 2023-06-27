/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include <ClipInstance.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <ParamManager.h>
#include "MIDIInstrument.h"
#include "midiengine.h"
#include "storagemanager.h"
#include <string.h>
#include "functions.h"
#include "song.h"
#include "MIDIParam.h"
#include "View.h"
#include "ActionLogger.h"
#include "numericdriver.h"
#include "matrixdriver.h"
#include "UI.h"
#include "Buttons.h"
#include "ModelStack.h"
#include "MIDIParamCollection.h"
#include "ParamSet.h"

#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "cfunctions.h"
}

int16_t lastNoteOffOrder = 1;

MIDIInstrument::MIDIInstrument() : NonAudioInstrument(INSTRUMENT_TYPE_MIDI_OUT) {
	channelSuffix = -1;
	modKnobMode = 0;
	memset(modKnobCCAssignments, CC_NUMBER_NONE, sizeof(modKnobCCAssignments));

	for (int c = 1; c <= 14; c++) {
		mpeOutputMemberChannels[c].lastNoteCode = 32767;
		mpeOutputMemberChannels[c].noteOffOrder = 0;
	}
}

// Returns whether any change made. For MIDI Instruments, this has no consequence
bool MIDIInstrument::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                            ModelStackWithThreeMainThings* modelStack) {

	if (on) {
		if (currentUIMode == UI_MODE_NONE) {

			if (getCurrentUI()->toClipMinder()) {
				currentUIMode = UI_MODE_SELECTING_MIDI_CC;

				int cc = modKnobCCAssignments[modKnobMode * NUM_PHYSICAL_MOD_KNOBS + whichModEncoder];

				bool automationExists = doesAutomationExistOnMIDIParam(modelStack, cc);
				InstrumentClipMinder::editingMIDICCForWhichModKnob = whichModEncoder;
				InstrumentClipMinder::drawMIDIControlNumber(cc, automationExists);
				return true;
			}
			else return false;
		}
		else return false;
	}

	// De-press
	else {
		if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
			currentUIMode = UI_MODE_NONE;
#if HAVE_OLED
			OLED::removePopup();
#else
			InstrumentClipMinder::redrawNumericDisplay();
#endif
		}
		return false;
	}
}

bool MIDIInstrument::doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int cc) {
	bool automationExists = false;

	ModelStackWithAutoParam* modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(cc, modelStack);
	if (modelStackWithAutoParam->autoParam) {
		automationExists = modelStackWithAutoParam->autoParam->isAutomated();
	}

	return automationExists;
}

void MIDIInstrument::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	// Editing CC
	if (DELUGE_MODEL == DELUGE_MODEL_40_PAD && whichModButton == modKnobMode) {
		if (on) {
			currentUIMode = UI_MODE_SELECTING_MIDI_CC;
			InstrumentClipMinder::editingMIDICCForWhichModKnob = 255;
			return;
		}
	}

	// Otherwise, if we're leaving this mod function or anything else is happening, we want to be sure that stutter has stopped
	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		currentUIMode = UI_MODE_NONE;
#if HAVE_OLED
		OLED::removePopup();
#else
		InstrumentClipMinder::redrawNumericDisplay();
#endif
	}
}

ModelStackWithAutoParam* MIDIInstrument::getParamFromModEncoder(int whichModEncoder,
                                                                ModelStackWithThreeMainThings* modelStack,
                                                                bool allowCreation) {

	if (!modelStack
	         ->paramManager) { // Could be NULL - if the user is holding down an audition pad in Arranger, and we have no Clips
noParam:
		return modelStack->addParamCollectionAndId(NULL, NULL, 0)->addAutoParam(NULL); // "No param"
	}

	int paramId = modKnobCCAssignments[modKnobMode * NUM_PHYSICAL_MOD_KNOBS + whichModEncoder];

	return getParamToControlFromInputMIDIChannel(paramId, modelStack);
}

// modelStack->autoParam will be NULL in this rare case!!
int MIDIInstrument::getKnobPosForNonExistentParam(int whichModEncoder, ModelStackWithAutoParam* modelStack) {
	if (modelStack->autoParam
	    && (modelStack->paramId < NUM_REAL_CC_NUMBERS || modelStack->paramId == CC_NUMBER_PITCH_BEND))
		return 0;
	else return ModControllable::getKnobPosForNonExistentParam(whichModEncoder, modelStack);
}

ModelStackWithAutoParam*
MIDIInstrument::getParamToControlFromInputMIDIChannel(int cc, ModelStackWithThreeMainThings* modelStack) {

	if (!modelStack
	         ->paramManager) { // Could be NULL - if the user is holding down an audition pad in Arranger, and we have no Clips
noParam:
		return modelStack->addParamCollectionAndId(NULL, NULL, 0)->addAutoParam(NULL); // "No param"
	}

	ParamCollectionSummary* summary;
	int paramId = cc;

	switch (cc) {
	case CC_NUMBER_PITCH_BEND:
		paramId = 0;
		goto expressionParam;

	case 74:
		paramId = 1;
		goto expressionParam;

	case CC_NUMBER_AFTERTOUCH:
		paramId = 2;
expressionParam:
		modelStack->paramManager->ensureExpressionParamSetExists(); // Allowed to fail
		summary = modelStack->paramManager->getExpressionParamSetSummary();
		if (!summary->paramCollection) goto noParam;
		break;

	case CC_NUMBER_NONE:
		goto noParam;

	default:
		summary = modelStack->paramManager->getMIDIParamCollectionSummary();
		break;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	return summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId,
	    true); // Yes we do want to force creating it even if we're not recording - so the level indicator can update for the user
}

void MIDIInstrument::ccReceivedFromInputMIDIChannel(int cc, int value, ModelStackWithTimelineCounter* modelStack) {

	int32_t valueBig = (value - 64) << 25;

	processParamFromInputMIDIChannel(cc, valueBig, modelStack);
}

int MIDIInstrument::getOutputMasterChannel() {
	switch (channel) {
	case MIDI_CHANNEL_MPE_LOWER_ZONE:
		return 0;

	case MIDI_CHANNEL_MPE_UPPER_ZONE:
		return 15;

	default:
		return channel;
	}
}

void MIDIInstrument::monophonicExpressionEvent(int newValue, int whichExpressionDimension) {

	int masterChannel = getOutputMasterChannel();

	switch (whichExpressionDimension) {
	case 0: {
		int valueSmall = (newValue >> 18) + 8192;
		midiEngine.sendPitchBend(masterChannel, valueSmall & 127, valueSmall >> 7, channel);
		break;
	}

	case 1:
		midiEngine.sendCC(masterChannel, 74, (newValue >> 25) + 64, channel);
		break;

	case 2:
		midiEngine.sendChannelAftertouch(masterChannel, newValue >> 24, channel);
		break;

	default:
		__builtin_unreachable();
	}
}

bool MIDIInstrument::setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs) {

	InstrumentClip* newInstrumentClip = (InstrumentClip*)modelStack->getTimelineCounter();
	InstrumentClip* oldInstrumentClip = (InstrumentClip*)activeClip;

	bool shouldSendPGMs = (maySendMIDIPGMs && activeClip && activeClip != newInstrumentClip
	                       && (newInstrumentClip->midiPGM != oldInstrumentClip->midiPGM
	                           || newInstrumentClip->midiSub != oldInstrumentClip->midiSub
	                           || newInstrumentClip->midiBank != oldInstrumentClip->midiBank));

	bool clipChanged = NonAudioInstrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (shouldSendPGMs) {
		sendMIDIPGM();
	}

	return clipChanged;
}

void MIDIInstrument::sendMIDIPGM() {
	if (activeClip) ((InstrumentClip*)activeClip)->sendMIDIPGM();
}

bool MIDIInstrument::writeDataToFile(Clip* clipForSavingOutputOnly, Song* song) {
	//NonAudioInstrument::writeDataToFile(clipForSavingOutputOnly, song); // Nope, this gets called within the below call
	writeMelodicInstrumentAttributesToFile(clipForSavingOutputOnly, song);

	if (editedByUser) { // Otherwise, there'll be nothing in here
		storageManager.writeOpeningTagEnd();
		storageManager.writeOpeningTag("modKnobs");
		for (int m = 0; m < NUM_MOD_BUTTONS * NUM_PHYSICAL_MOD_KNOBS; m++) {

			int cc = modKnobCCAssignments[m];

			storageManager.writeOpeningTagBeginning("modKnob");
			if (cc == CC_NUMBER_NONE) storageManager.writeAttribute("cc", "none");
			else if (cc == CC_NUMBER_PITCH_BEND) storageManager.writeAttribute("cc", "bend");
			else if (cc == CC_NUMBER_AFTERTOUCH) storageManager.writeAttribute("cc", "aftertouch");
			else storageManager.writeAttribute("cc", cc);
			storageManager.closeTag();
		}
		storageManager.writeClosingTag("modKnobs");
	}
	else {
		if (clipForSavingOutputOnly || !midiInput.containsSomething())
			return false; // If we don't need to write a "device" tag, opt not to end the opening tag

		storageManager.writeOpeningTagEnd();
	}

	MelodicInstrument::writeMelodicInstrumentTagsToFile(clipForSavingOutputOnly, song);
	return true;
}

bool MIDIInstrument::readTagFromFile(char const* tagName) {

	char const* subSlotXMLTag = getSubSlotXMLTag();

	if (!strcmp(tagName, "modKnobs")) {
		readModKnobAssignmentsFromFile(
		    MAX_SEQUENCE_LENGTH); // Not really ideal, but we don't know the number and can't easily get it. I think it'd only be relevant for pre-V2.0 song file... maybe?
	}
	else if (!strcmp(tagName, "zone")) {
		char const* text = storageManager.readTagOrAttributeValue();
		if (!strcmp(text, "lower")) {
			channel = MIDI_CHANNEL_MPE_LOWER_ZONE;
		}
		else if (!strcmp(text, "upper")) {
			channel = MIDI_CHANNEL_MPE_UPPER_ZONE;
		}
	}
	else if (!strcmp(tagName, subSlotXMLTag)) {
		channelSuffix = storageManager.readTagOrAttributeValueInt();
	}
	else if (NonAudioInstrument::readTagFromFile(tagName)) return true;
	else return false;

	storageManager.exitTag();
	return true;
}

// paramManager is sometimes NULL (when called from the above function), for reasons I've kinda forgotten, yet everything seems to still work...
int MIDIInstrument::readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos,
                                                   ParamManagerForTimeline* paramManager) {
	int m = 0;
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "modKnob")) {
			MIDIParamCollection* midiParamCollection = NULL;
			if (paramManager) midiParamCollection = paramManager->getMIDIParamCollection();
			int error = storageManager.readMIDIParamFromFile(readAutomationUpToPos, midiParamCollection,
			                                                 &modKnobCCAssignments[m]);
			if (error) return error;
			m++;
		}

		storageManager.exitTag();
		if (m >= NUM_MOD_BUTTONS * NUM_PHYSICAL_MOD_KNOBS) break;
	}

	editedByUser = true;
	return NO_ERROR;
}

int MIDIInstrument::changeControlNumberForModKnob(int offset, int whichModEncoder, int modKnobMode) {
	int8_t* cc = &modKnobCCAssignments[modKnobMode * NUM_PHYSICAL_MOD_KNOBS + whichModEncoder];

	int newCC = *cc;

	newCC += offset;
	if (newCC < 0) newCC += NUM_CC_NUMBERS_INCLUDING_FAKE;
	else if (newCC >= NUM_CC_NUMBERS_INCLUDING_FAKE) newCC -= NUM_CC_NUMBERS_INCLUDING_FAKE;

	*cc = newCC;

	editedByUser = true;
	return newCC;
}

int MIDIInstrument::getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int direction, int startAt,
                                     int stopAt) {

	int proposedCC = startAt;

	while (true) {
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    getParamToControlFromInputMIDIChannel(proposedCC, modelStack);
		if (!modelStackWithAutoParam->autoParam || !modelStackWithAutoParam->autoParam->isAutomated())
			return proposedCC;

		proposedCC += direction;

		if (proposedCC < 0) proposedCC += CC_NUMBER_NONE;
		else if (proposedCC >= CC_NUMBER_NONE) proposedCC -= CC_NUMBER_NONE;

		if (proposedCC == stopAt) return -1;
	}

	// It does always return something, above
}

// Returns error code
int MIDIInstrument::moveAutomationToDifferentCC(int oldCC, int newCC, ModelStackWithThreeMainThings* modelStack) {

	ModelStackWithAutoParam* modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(oldCC, modelStack);

	AutoParam* oldParam = modelStackWithAutoParam->autoParam;
	if (!oldParam) return NO_ERROR;

	AutoParamState state;
	oldParam->swapState(&state, modelStackWithAutoParam);

	// Delete or clear old parameter
	MIDIParamCollection* midiParamCollection = modelStackWithAutoParam->paramManager->getMIDIParamCollection();

	// CC (besides 74)
	if (modelStackWithAutoParam->paramCollection == midiParamCollection) {
		midiParamCollection->params.deleteAtKey(oldCC);
	}

	// Expression param
	else {
#if ALPHA_OR_BETA_VERSION
		if (modelStackWithAutoParam->paramCollection != modelStack->paramManager->getExpressionParamSet())
			numericDriver.freezeWithError("E415");
		if (modelStackWithAutoParam->paramId >= NUM_EXPRESSION_DIMENSIONS) numericDriver.freezeWithError("E416");
#endif
		((ExpressionParamSet*)modelStackWithAutoParam->paramCollection)
		    ->params[modelStackWithAutoParam->paramId]
		    .setCurrentValueBasicForSetup(0);
	}

	modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(newCC, modelStack);
	AutoParam* newParam = modelStackWithAutoParam->autoParam;
	if (!newParam) return ERROR_INSUFFICIENT_RAM;

	newParam->swapState(&state, modelStackWithAutoParam);

	return NO_ERROR;
}

int MIDIInstrument::moveAutomationToDifferentCC(int offset, int whichModEncoder, int modKnobMode,
                                                ModelStackWithThreeMainThings* modelStack) {

	int8_t* cc = &modKnobCCAssignments[modKnobMode * NUM_PHYSICAL_MOD_KNOBS + whichModEncoder];

	if (*cc >= CC_NUMBER_NONE) return *cc;

	int newCC = *cc + offset;
	if (newCC < 0) newCC += CC_NUMBER_NONE;
	else if (newCC >= CC_NUMBER_NONE) newCC -= CC_NUMBER_NONE;

	// Need to pick a new cc which is blank on all Clips' ParamManagers with this Instrument
	// For each Clip in session and arranger for specific Output (that Output is "this")
	int numElements = modelStack->song->sessionClips.getNumElements();
	bool doingArrangementClips = false;
traverseClips:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = modelStack->song->sessionClips.getClipAtIndex(c);
			if (clip->output != this) continue;
		}
		else {
			ClipInstance* clipInstance = clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
			clip = clipInstance->clip;
		}

		newCC = getFirstUnusedCC(modelStack, offset, newCC, *cc);
		if (newCC == -1) return -1;
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = clipInstances.getNumElements();
		goto traverseClips;
	}

	// And then tell all Clips' ParamManagers with this Instrument to change that CC
	// For each Clip in session and arranger for specific Output (that Output is "this")
	numElements = modelStack->song->sessionClips.getNumElements();
	doingArrangementClips = false;
traverseClips2:
	for (int c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = modelStack->song->sessionClips.getClipAtIndex(c);
			if (clip->output != this) continue;
		}
		else {
			ClipInstance* clipInstance = clipInstances.getElement(c);
			if (!clipInstance->clip) continue;
			if (!clipInstance->clip->isArrangementOnlyClip()) continue;
			clip = clipInstance->clip;
		}

		moveAutomationToDifferentCC(*cc, newCC, modelStack);
	}
	if (!doingArrangementClips) {
		doingArrangementClips = true;
		numElements = clipInstances.getNumElements();
		goto traverseClips2;
	}

	*cc = newCC;
	editedByUser = true;
	return newCC;
}

void MIDIInstrument::offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                       MIDIDevice* fromDevice, bool on, int receivedChannel, int note, int velocity,
                                       bool shouldRecordNotes, bool* doingMidiThru) {

	if (midiInput.channelOrZone == receivedChannel) {

		// If it's a MIDI Clip, and it's outputting on the same channel as this MIDI message came in, don't do MIDI thru!
		if (doingMidiThru && type == INSTRUMENT_TYPE_MIDI_OUT
		    && receivedChannel
		           == channel) { // We'll just say don't do anything to midi-thru if any MPE in the picture, for now
			*doingMidiThru = false;
		}
	}

	NonAudioInstrument::offerReceivedNote(modelStackWithTimelineCounter, fromDevice, on, receivedChannel, note,
	                                      velocity, shouldRecordNotes, doingMidiThru);
}

void MIDIInstrument::noteOnPostArp(int noteCodePostArp, ArpNote* arpNote) {

	ArpeggiatorSettings* arpSettings = NULL;
	if (activeClip) {
		arpSettings = &((InstrumentClip*)activeClip)->arpSettings;
	}

	bool arpIsOn = arpSettings && arpSettings->mode;
	int outputMemberChannel;

	// If no MPE, nice and simple.
	if (!sendsToMPE()) {
		outputMemberChannel = channel;
	}

	// Or if MPE, we have to decide on a member channel to assign note to...
	else {

		int lowestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                              ? 1
		                              : MIDIDeviceManager::highestLastMemberChannelOfUpperZoneOnConnectedOutput;
		int highestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                               ? MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput
		                               : 14;

		uint8_t numNotesPreviouslyActiveOnMemberChannel[15];
		memset(numNotesPreviouslyActiveOnMemberChannel, 0, sizeof(numNotesPreviouslyActiveOnMemberChannel));

		int outputMemberChannelWithNoteSharingInputMemberChannel = 16; // 16 means "none", I think

		if (!arpIsOn) {
			// Count up notes per member channel. This traversal will *not* find the new note that we're switching on, which will have had its toMIDIChannel set to 16 by Arpeggiator (we'll decide and set it below).
			for (int n = 0; n < arpeggiator.notes.getNumElements(); n++) {
				ArpNote* thisArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				if (thisArpNote->outputMemberChannel >= 1 && thisArpNote->outputMemberChannel <= 14) {
					numNotesPreviouslyActiveOnMemberChannel[thisArpNote->outputMemberChannel]++;

					// If this note is coming in live from the same member channel as the one we wish to switch on now, that's a good clue that we should group them together at the output. (Final decision to be made further below.)
					if (thisArpNote->inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL]
					    == arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL]) {
						outputMemberChannelWithNoteSharingInputMemberChannel = thisArpNote->outputMemberChannel;
					}
				}
			}
		}

		// See which member channel fits criteria best. The MPE spec has guidelines about what criteria to use.
		uint32_t bestGoodnessFound = 0;
		bool foundOneWithRightNoteCode = false;

		for (int c = lowestMemberChannel; c <= highestMemberChannel; c++) {

			// If it has the right noteCode, that trumps everything.
			if (mpeOutputMemberChannels[c].lastNoteCode == noteCodePostArp) {
				outputMemberChannel = c;
				break;
			}

			uint16_t timeSinceNoteOff = lastNoteOffOrder - mpeOutputMemberChannels[c].noteOffOrder;
			uint32_t goodness = ((256 - numNotesPreviouslyActiveOnMemberChannel[c]) << 16) | timeSinceNoteOff;

			if (goodness > bestGoodnessFound) {
				outputMemberChannel = c;
				bestGoodnessFound = goodness;
			}
		}

		// If we weren't able to get an output member channel all to ourselves, a better option (if it exists) would be to
		// group with note(s) which shared an input member channel
		if (numNotesPreviouslyActiveOnMemberChannel[outputMemberChannel]
		    && outputMemberChannelWithNoteSharingInputMemberChannel < 16) {
			outputMemberChannel = outputMemberChannelWithNoteSharingInputMemberChannel;
		}

		// TODO: It'd be good to be able to group them according to them having similar MPE data, which could happen if they were originally recorded via the same input member channel.
		// This would actually be really easy to do reasonably well.

		// Ok. We have our new member channel.

		arpNote->outputMemberChannel = outputMemberChannel;
		arpeggiator.outputMIDIChannelForNoteCurrentlyOnPostArp = outputMemberChannel; // Needed if arp on

		int16_t mpeValuesAverage[NUM_EXPRESSION_DIMENSIONS]; // We may fill this up and point to it, or not
		int16_t const* mpeValuesToUse = arpNote->mpeValues;

		// If other notes are already being output on this member channel, we'll need to average MPE values and stuff
		if (numNotesPreviouslyActiveOnMemberChannel[outputMemberChannel]) {
			int numNotesFound = 0;
			int32_t mpeValuesSum
			    [NUM_EXPRESSION_DIMENSIONS]; // We'll be summing 16-bit values into this 32-bit container, so no overflowing
			memset(mpeValuesSum, 0, sizeof(mpeValuesSum));

			for (int n = 0; n < arpeggiator.notes.getNumElements();
			     n++) { // This traversal will include the original note, which will get counted up too
				ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				if (lookingAtArpNote->outputMemberChannel == outputMemberChannel) {
					numNotesFound++;
					for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
						mpeValuesSum[m] += lookingAtArpNote->mpeValues[m];
					}
				}
			}

			// Work out averages
			for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
				mpeValuesAverage[m] = mpeValuesSum[m] / numNotesFound;
			}

			mpeValuesToUse = mpeValuesAverage; // And point to those averages, to actually use

			// And we'll carry on below, outputting those average values.
			// There's a chance that this new average will be the same as whatever previous ones were output on this member channel. We could check for this and omit re-sending them
			// in that case, but there's very little to be gained by that added complexity.
		}

		// Ok, now we'll output MPE values - which will either be the values for this exact note we're outputting, or if it's sharing a member channel, it'll be the average values we
		// worked out above.
		outputAllMPEValuesOnMemberChannel(mpeValuesToUse, outputMemberChannel);
	}

	midiEngine.sendNote(true, noteCodePostArp, arpNote->velocity, outputMemberChannel, channel);
}

// Will store them too. Only for when we definitely want to send all three.
// And obviously you can't call this unless you know that this Instrument sendsToMPE().
void MIDIInstrument::outputAllMPEValuesOnMemberChannel(int16_t const* mpeValuesToUse, int outputMemberChannel) {
	{ // X
		int outputValue14 = mpeValuesToUse[0] >> 2;
		mpeOutputMemberChannels[outputMemberChannel].lastXValueSent = outputValue14;
		int outputValue14Unsigned = outputValue14 + 8192;
		midiEngine.sendPitchBend(outputMemberChannel, outputValue14Unsigned & 127, outputValue14Unsigned >> 7, channel);
	}

	{ // Y
		int outputValue7 = mpeValuesToUse[1] >> 9;
		mpeOutputMemberChannels[outputMemberChannel].lastYAndZValuesSent[0] = outputValue7;
		midiEngine.sendCC(outputMemberChannel, 74, outputValue7 + 64, channel);
	}

	{ // Z
		int outputValue7 = mpeValuesToUse[2] >> 8;
		mpeOutputMemberChannels[outputMemberChannel].lastYAndZValuesSent[1] = outputValue7;
		midiEngine.sendChannelAftertouch(outputMemberChannel, outputValue7, channel);
	}
}

void MIDIInstrument::noteOffPostArp(int noteCodePostArp, int oldOutputMemberChannel, int velocity) {

	// If no MPE, nice and simple
	if (!sendsToMPE()) {
		midiEngine.sendNote(false, noteCodePostArp, velocity, channel, MIDI_OUTPUT_FILTER_NO_MPE);
	}

	// Or, MPE
	else {
		if (oldOutputMemberChannel <= 14) {
			mpeOutputMemberChannels[oldOutputMemberChannel].lastNoteCode = noteCodePostArp;
			mpeOutputMemberChannels[oldOutputMemberChannel].noteOffOrder = lastNoteOffOrder++;

			midiEngine.sendNote(false, noteCodePostArp, velocity, oldOutputMemberChannel, channel);

			// And now, if this note was sharing a member channel with any others, we want to send MPE values for those new averages
			int numNotesFound = 0;
			int32_t mpeValuesSum
			    [NUM_EXPRESSION_DIMENSIONS]; // We'll be summing 16-bit values into this 32-bit container, so no overflowing
			memset(mpeValuesSum, 0, sizeof(mpeValuesSum));

			for (
			    int n = 0; n < arpeggiator.notes.getNumElements();
			    n++) { // This traversal will not include the original note, which has already been deleted from the array.
				ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				if (lookingAtArpNote->outputMemberChannel == oldOutputMemberChannel) {
					numNotesFound++;
					for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
						mpeValuesSum[m] += lookingAtArpNote->mpeValues[m];
					}
				}
			}

			if (numNotesFound) {
				// Work out averages
				int16_t mpeValuesAverage[NUM_EXPRESSION_DIMENSIONS];
				for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
					mpeValuesAverage[m] = mpeValuesSum[m] / numNotesFound;
				}
				outputAllMPEValuesOnMemberChannel(mpeValuesAverage, oldOutputMemberChannel);
			}
		}
	}
}

void MIDIInstrument::allNotesOff() {
	arpeggiator.reset();

	// If no MPE, nice and simple
	if (!sendsToMPE()) {
		midiEngine.sendAllNotesOff(channel, MIDI_OUTPUT_FILTER_NO_MPE);
	}

	// Otherwise, got to send message on all MPE member channels. At least I think that's right. The MPE spec talks about sending "all *sounds* off" on just the master channel,
	// but doesn't mention all *notes* off.
	else {
		// We'll send on the master channel as well as the member channels.
		int lowestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                              ? 0
		                              : MIDIDeviceManager::highestLastMemberChannelOfUpperZoneOnConnectedOutput;
		int highestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                               ? MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput
		                               : 15;

		for (int c = lowestMemberChannel; c <= highestMemberChannel; c++) {
			midiEngine.sendAllNotesOff(c, channel);
		}
	}
}

uint8_t const shiftAmountsFrom16Bit[] = {2, 9, 8};

// The arpNote actually already contains a stored version of value32 - except it's been reduced to 16-bit, so we may as well use the 32-bit version here.
// Although, could it have even got more than 14 bits of meaningful value in the first place?
void MIDIInstrument::polyphonicExpressionEventPostArpeggiator(int value32, int noteCodeAfterArpeggiation,
                                                              int whichExpressionDimension, ArpNote* arpNote) {

	// If we don't have MPE output...
	if (!sendsToMPE()) {

		// We can only send Z - and that's as polyphonic aftertouch
		if (whichExpressionDimension == 2) {
			midiEngine.sendPolyphonicAftertouch(channel, value32 >> 24, noteCodeAfterArpeggiation,
			                                    MIDI_OUTPUT_FILTER_NO_MPE);
		}
	}

	// Or if we do have MPE output...
	else {
		int memberChannel = arpNote->outputMemberChannel;

		// Are multiple notes sharing the same output member channel?
		ArpeggiatorSettings* settings = getArpSettings();
		if (!settings || !settings->mode) { // Only if not arpeggiating...
			int numNotesFound = 0;
			int32_t mpeValuesSum = 0; // We'll be summing 16-bit values into this 32-bit container, so no overflowing

			for (int n = 0; n < arpeggiator.notes.getNumElements();
			     n++) { // This traversal will include the original note, which will get counted up too
				ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				if (lookingAtArpNote->outputMemberChannel == memberChannel) {
					numNotesFound++;
					mpeValuesSum += lookingAtArpNote->mpeValues[whichExpressionDimension];
				}
			}

			// If there in fact are multiple notes sharing the channel, to combine...
			if (numNotesFound > 1) {
				int averageValue16 = mpeValuesSum / numNotesFound;

				int averageValue7Or14 = averageValue16 >> shiftAmountsFrom16Bit[whichExpressionDimension];
				int lastValue7Or14 =
				    whichExpressionDimension
				        ? mpeOutputMemberChannels[memberChannel].lastYAndZValuesSent[whichExpressionDimension - 1]
				        : mpeOutputMemberChannels[memberChannel].lastXValueSent;

				// If there's been no actual change, don't send anything
				if (averageValue7Or14 == lastValue7Or14) return;

				// Otherwise, do send this average value
				value32 = averageValue16 << 16;
			}
		}

		switch (whichExpressionDimension) {
		case 0: { // X
			int value14 = (value32 >> 18);
			mpeOutputMemberChannels[memberChannel].lastXValueSent = value14;
			int value14Unsigned = value14 + 8192;
			midiEngine.sendPitchBend(memberChannel, value14Unsigned & 127, value14Unsigned >> 7, channel);
			break;
		}

		case 1: { // Y
			int value7 = value32 >> 25;
			mpeOutputMemberChannels[memberChannel].lastYAndZValuesSent[0] = value7;
			midiEngine.sendCC(memberChannel, 74, value7 + 64, channel);
			break;
		}

		case 2: { // Z
			int value7 = value32 >> 24;
			mpeOutputMemberChannels[memberChannel].lastYAndZValuesSent[1] = value7;
			midiEngine.sendChannelAftertouch(memberChannel, value7, channel);
			break;
		}
		default:
			__builtin_unreachable();
		}
	}
}
