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

#include "model/instrument/midi_instrument.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/midi/label/midi_label.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_set.h"
#include "storage/storage_manager.h"
#include <cstring>

int16_t lastNoteOffOrder = 1;

MIDIInstrument::MIDIInstrument()
    : NonAudioInstrument(OutputType::MIDI_OUT), mpeOutputMemberChannels(),
      ratio(float(cachedBendRanges[BEND_RANGE_FINGER_LEVEL]) / float(cachedBendRanges[BEND_RANGE_MAIN])),
      modKnobCCAssignments() {
	modKnobMode = 0;
	modKnobCCAssignments.fill(CC_NUMBER_NONE);
}

// Returns whether any change made. For MIDI Instruments, this has no consequence
bool MIDIInstrument::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                            ModelStackWithThreeMainThings* modelStack) {

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

bool MIDIInstrument::doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int32_t cc) {
	bool automationExists = false;

	ModelStackWithAutoParam* modelStackWithAutoParam = getParamToControlFromInputMIDIChannel(cc, modelStack);
	if (modelStackWithAutoParam->autoParam) {
		automationExists = modelStackWithAutoParam->autoParam->isAutomated();
	}

	return automationExists;
}

void MIDIInstrument::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
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

ModelStackWithAutoParam* MIDIInstrument::getParamFromModEncoder(int32_t whichModEncoder,
                                                                ModelStackWithThreeMainThings* modelStack,
                                                                bool allowCreation) {

	if (!modelStack->paramManager) { // Could be NULL - if the user is holding down an audition pad in Arranger, and we
		                             // have no Clips
noParam:
		return modelStack->addParamCollectionAndId(NULL, NULL, 0)->addAutoParam(NULL); // "No param"
	}

	int32_t paramId = modKnobCCAssignments[modKnobMode * kNumPhysicalModKnobs + whichModEncoder];

	return getParamToControlFromInputMIDIChannel(paramId, modelStack);
}

// modelStack->autoParam will be NULL in this rare case!!
int32_t MIDIInstrument::getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) {
	if (modelStack->autoParam
	    && (modelStack->paramId < kNumRealCCNumbers || modelStack->paramId == CC_NUMBER_PITCH_BEND)) {
		return 0;
	}
	else {
		return ModControllable::getKnobPosForNonExistentParam(whichModEncoder, modelStack);
	}
}

ModelStackWithAutoParam*
MIDIInstrument::getParamToControlFromInputMIDIChannel(int32_t cc, ModelStackWithThreeMainThings* modelStack) {

	if (!modelStack->paramManager) { // Could be NULL - if the user is holding down an audition pad in Arranger, and we
		                             // have no Clips
noParam:
		return modelStack->addParamCollectionAndId(NULL, NULL, 0)->addAutoParam(NULL); // "No param"
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

void MIDIInstrument::ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value,
                                                    ModelStackWithTimelineCounter* modelStack) {

	int32_t valueBig = (value - 64) << 25;

	processParamFromInputMIDIChannel(cc, valueBig, modelStack);
}

int32_t MIDIInstrument::getOutputMasterChannel() {
	auto c = getChannel();
	switch (c) {
	case MIDI_CHANNEL_MPE_LOWER_ZONE:
		return 0;

	case MIDI_CHANNEL_MPE_UPPER_ZONE:
		return 15;

	default:
		return c;
	}
}

void MIDIInstrument::monophonicExpressionEvent(int32_t newValue, int32_t whichExpressionDimension) {
	lastMonoExpression[whichExpressionDimension] = newValue;
	sendMonophonicExpressionEvent(whichExpressionDimension);
}

void MIDIInstrument::sendMonophonicExpressionEvent(int32_t whichExpressionDimension) {

	int32_t masterChannel = getOutputMasterChannel();

	switch (whichExpressionDimension) {
	case X_PITCH_BEND: {
		int32_t newValue = add_saturation(lastCombinedPolyExpression[whichExpressionDimension],
		                                  lastMonoExpression[whichExpressionDimension]);
		int32_t valueSmall = (newValue >> 18) + 8192;
		midiEngine.sendPitchBend(this, masterChannel, valueSmall & 127, valueSmall >> 7, getChannel());
		break;
	}

	case Y_SLIDE_TIMBRE: {
		// mono y expression is limited to positive values
		// this means that without sending CC1 on the master channel poly expression below 64 will
		// send as mod wheel 0. However this is better than the alternative of sending erroneous values
		// because the sound engine initializes MPE-Y as 0 (e.g. a CC value of 64)
		int32_t polyPart = (lastCombinedPolyExpression[whichExpressionDimension] >> 24);
		int32_t monoPart = lastMonoExpression[whichExpressionDimension] >> 24;
		int32_t newValue = std::clamp<int32_t>(polyPart + monoPart, 0, 127);
		// send CC1 for monophonic expression - monophonic synths won't do anything useful with CC74

		midiEngine.sendCC(this, masterChannel, CC_EXTERNAL_MOD_WHEEL, newValue, getChannel());
		break;
	}
	case Z_PRESSURE: {
		int32_t newValue = add_saturation(lastCombinedPolyExpression[whichExpressionDimension],
		                                  lastMonoExpression[whichExpressionDimension])
		                   >> 24;
		midiEngine.sendChannelAftertouch(this, masterChannel, newValue, getChannel());
		break;
	}
	default:
		__builtin_unreachable();
	}
}

bool MIDIInstrument::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {
	bool shouldSendPGMs;
	if (modelStack) {
		InstrumentClip* newInstrumentClip = (InstrumentClip*)modelStack->getTimelineCounter();
		InstrumentClip* oldInstrumentClip = (InstrumentClip*)activeClip;

		shouldSendPGMs = (maySendMIDIPGMs != PgmChangeSend::NEVER && activeClip && activeClip != newInstrumentClip
		                  && (newInstrumentClip->midiPGM != oldInstrumentClip->midiPGM
		                      || newInstrumentClip->midiSub != oldInstrumentClip->midiSub
		                      || newInstrumentClip->midiBank != oldInstrumentClip->midiBank));
	}
	else {
		shouldSendPGMs = false;
	}
	bool clipChanged = NonAudioInstrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (shouldSendPGMs) {
		sendMIDIPGM();
	}
	if (clipChanged) {
		if (modelStack) {
			ParamManager* paramManager = &modelStack->getTimelineCounter()->paramManager;
			ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
			if (expressionParams) {
				cachedBendRanges[BEND_RANGE_MAIN] = expressionParams->bendRanges[BEND_RANGE_MAIN];
				cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL];
				ratio = float(cachedBendRanges[BEND_RANGE_FINGER_LEVEL]) / float(cachedBendRanges[BEND_RANGE_MAIN]);
			}
		}
		else {
			allNotesOff();
			for (int i = 0; i < kNumExpressionDimensions; i++) {
				lastCombinedPolyExpression[i] = 0;
				sendMonophonicExpressionEvent(i);
			}
		}
	}

	return clipChanged;
}

void MIDIInstrument::sendMIDIPGM() {
	if (activeClip) {
		((InstrumentClip*)activeClip)->sendMIDIPGM();
	}
}

bool MIDIInstrument::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {
	// NonAudioInstrument::writeDataToFile(writer, clipForSavingOutputOnly, song); // Nope, this gets called within the
	// below call
	writeMelodicInstrumentAttributesToFile(writer, clipForSavingOutputOnly, song);

	if (editedByUser || clipForSavingOutputOnly) { // Otherwise, there'll be nothing in here
		writer.writeOpeningTagEnd();
		writer.writeOpeningTag("modKnobs");
		for (int32_t m = 0; m < kNumModButtons * kNumPhysicalModKnobs; m++) {

			int32_t cc = modKnobCCAssignments[m];

			writer.writeOpeningTagBeginning("modKnob");
			if (cc == CC_NUMBER_NONE) {
				writer.writeAttribute("cc", "none");
			}
			else if (cc == CC_NUMBER_PITCH_BEND) {
				writer.writeAttribute("cc", "bend");
			}
			else if (cc == CC_NUMBER_AFTERTOUCH) {
				writer.writeAttribute("cc", "aftertouch");
			}
			else {
				writer.writeAttribute("cc", cc);
			}
			writer.closeTag();
		}
		writer.writeClosingTag("modKnobs");

		writer.writeOpeningTagBeginning("polyToMonoConversion");
		writer.writeAttribute("aftertouch", (int32_t)collapseAftertouch);
		writer.writeAttribute("mpe", (int32_t)collapseMPE);
		writer.writeAttribute("yCC", (int32_t)outputMPEY);
		writer.closeTag();

		writeDeviceDefinitionFile(writer, true);
	}
	else {
		if (!clipForSavingOutputOnly && !midiInput.containsSomething()) {
			// If we don't need to write a "device" tag, opt not to end the opening tag, unless we're saving the output
			// since then it's the whole tag
			return false;
		}

		writer.writeOpeningTagEnd();
	}

	MelodicInstrument::writeMelodicInstrumentTagsToFile(writer, clipForSavingOutputOnly, song);
	return true;
}

void MIDIInstrument::writeDeviceDefinitionFile(Serializer& writer, bool writeFileNameToPresetOrSong) {
	writer.writeOpeningTagBeginning("midiDevice");
	writer.writeOpeningTagEnd();

	if (writeFileNameToPresetOrSong) {
		writeDeviceDefinitionFileNameToPresetOrSong(writer);
	}

	writeCCLabelsToFile(writer);

	writer.writeClosingTag("midiDevice");
}

void MIDIInstrument::writeDeviceDefinitionFileNameToPresetOrSong(Serializer& writer) {
	writer.writeOpeningTagBeginning("definitionFile");
	if (deviceDefinitionFileName.isEmpty()) {
		writer.writeAttribute("name", "");
	}
	else {
		writer.writeAttribute("name", deviceDefinitionFileName.get());
	}
	writer.closeTag();
}

void MIDIInstrument::writeCCLabelsToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("ccLabels");
	for (int32_t i = 0; i < kNumRealCCNumbers; i++) {
		if (i != CC_EXTERNAL_MOD_WHEEL) {
			MIDILabel* midiLabel = midiLabelCollection.labels.getLabelFromCC(i);
			char ccNumber[10];
			intToString(i, ccNumber, 1);
			if (midiLabel) {
				writer.writeAttribute(ccNumber, midiLabel->name.get());
			}
			else {
				writer.writeAttribute(ccNumber, "");
			}
		}
	}
	writer.closeTag();
}

bool MIDIInstrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	char const* subSlotXMLTag = getSubSlotXMLTag();

	if (!strcmp(tagName, "modKnobs")) {
		readModKnobAssignmentsFromFile(
		    kMaxSequenceLength); // Not really ideal, but we don't know the number and can't easily get
		                         // it. I think it'd only be relevant for pre-V2.0 song file... maybe?
	}
	else if (!strcmp(tagName, "polyToMonoConversion")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "aftertouch")) {
				collapseAftertouch = (bool)reader.readTagOrAttributeValueInt();
				editedByUser = true;
			}
			else if (!strcmp(tagName, "mpe")) {
				collapseMPE = (bool)reader.readTagOrAttributeValueInt();
				editedByUser = true;
			}
			else if (!strcmp(tagName, "yCC")) {
				outputMPEY = static_cast<CCNumber>(reader.readTagOrAttributeValueInt());
				editedByUser = true;
			}
			else
				break;
		}
	}
	else if (!strcmp(tagName, "internalDest")) {
		char const* text = reader.readTagOrAttributeValue();
		if (!strcmp(text, "transpose")) {
			setChannel(MIDI_CHANNEL_TRANSPOSE);
		}
	}
	else if (!strcmp(tagName, "midiChannel")) {
		// fix for incorrect save files created on nightlies between 18/02/24 and 08/04/24
		setChannel(reader.readTagOrAttributeValueInt());
	}
	else if (!strcmp(tagName, "zone")) {
		char const* text = reader.readTagOrAttributeValue();
		if (!strcmp(text, "lower")) {
			setChannel(MIDI_CHANNEL_MPE_LOWER_ZONE);
		}
		else if (!strcmp(text, "upper")) {
			setChannel(MIDI_CHANNEL_MPE_UPPER_ZONE);
		}
	}
	else if (!strcmp(tagName, subSlotXMLTag)) {
		channelSuffix = reader.readTagOrAttributeValueInt();
	}
	else if (!strcmp(tagName, "midiDevice")) {
		readDeviceDefinitionFile(reader, true);
	}
	else if (NonAudioInstrument::readTagFromFile(reader, tagName)) {
		return true;
	}
	else {
		return false;
	}

	reader.exitTag();
	return true;
}

// paramManager is sometimes NULL (when called from the above function), for reasons I've kinda forgotten, yet
// everything seems to still work...
Error MIDIInstrument::readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos,
                                                     ParamManagerForTimeline* paramManager) {
	int32_t m = 0;
	char const* tagName;
	Deserializer& reader = *activeDeserializer;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "modKnob")) {
			MIDIParamCollection* midiParamCollection = NULL;
			if (paramManager) {
				midiParamCollection = paramManager->getMIDIParamCollection();
			}
			Error error =
			    readMIDIParamFromFile(reader, readAutomationUpToPos, midiParamCollection, &modKnobCCAssignments[m]);
			if (error != Error::NONE) {
				return error;
			}
			m++;
		}

		reader.exitTag();
		if (m >= kNumModButtons * kNumPhysicalModKnobs) {
			break;
		}
	}

	editedByUser = true;
	return Error::NONE;
}

Error MIDIInstrument::readDeviceDefinitionFile(Deserializer& reader, bool readFromPresetOrSong) {
	Error error = Error::FILE_UNREADABLE;
	loadDeviceDefinitionFile = false;

	char const* tagName;

	// step into any subtags
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "definitionFile")) {
			readDeviceDefinitionFileNameFromPresetOrSong(reader);
			// only flag definition file for loading if we aren't reading from preset or song
			// and definition file name isn't blank
			if (!deviceDefinitionFileName.isEmpty() && !readFromPresetOrSong) {
				loadDeviceDefinitionFile = true;
			}
		}
		// if we aren't reading from device definition file later, then try to read
		// device info now
		else if (!loadDeviceDefinitionFile) {
			if (!strcmp(tagName, "ccLabels")) {
				error = readCCLabelsFromFile(reader);
			}
		}
		reader.exitTag();
	}

	editedByUser = true;
	return error;
}

void MIDIInstrument::readDeviceDefinitionFileNameFromPresetOrSong(Deserializer& reader) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "name")) {
			reader.readTagOrAttributeValueString(&deviceDefinitionFileName);
		}
		reader.exitTag();
	}
}

Error MIDIInstrument::readCCLabelsFromFile(Deserializer& reader) {
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

		midiLabelCollection.labels.setOrCreateLabelForCC(cc, reader.readTagOrAttributeValue());

		error = Error::NONE;

		reader.exitTag();
	}

	return error;
}

// This has now mostly been replaced by an equivalent-ish function in InstrumentClip.
// Now, this will only ever be called in two scenarios:
// -- Pre-V2.0 files, so we know there's no mention of bend or aftertouch in this case where we have a ParamManager.
// -- When reading a MIDIInstrument, so we know there's no ParamManager (I checked), so no need to actually read the
// param.
Error MIDIInstrument::readMIDIParamFromFile(Deserializer& reader, int32_t readAutomationUpToPos,
                                            MIDIParamCollection* midiParamCollection, int8_t* getCC) {

	char const* tagName;
	int32_t cc = CC_NUMBER_NONE;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "cc")) {
			char const* contents = reader.readTagOrAttributeValue();
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
			// will be sent as mod wheel and also map to internal mono expression
			if (cc == CC_EXTERNAL_MOD_WHEEL) {
				cc = CC_NUMBER_Y_AXIS;
			}

			reader.exitTag("cc");
		}
		else if (!strcmp(tagName, "value")) {
			if (cc != CC_NUMBER_NONE && midiParamCollection) {

				MIDIParam* midiParam = midiParamCollection->params.getOrCreateParamFromCC(cc, 0);
				if (!midiParam) {
					return Error::INSUFFICIENT_RAM;
				}

				Error error = midiParam->param.readFromFile(reader, readAutomationUpToPos);
				if (error != Error::NONE) {
					return error;
				}
			}
			reader.exitTag("value");
		}
		else {
			reader.exitTag(tagName);
		}
	}

	if (getCC) {
		*getCC = cc;
	}

	return Error::NONE;
}

int32_t MIDIInstrument::changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode) {
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

	editedByUser = true;
	return newCC;
}

int32_t MIDIInstrument::getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt,
                                         int32_t stopAt) {

	int32_t proposedCC = startAt;

	while (true) {
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    getParamToControlFromInputMIDIChannel(proposedCC, modelStack);
		if (!modelStackWithAutoParam->autoParam || !modelStackWithAutoParam->autoParam->isAutomated()) {
			return proposedCC;
		}

		proposedCC += direction;

		if (proposedCC < 0) {
			proposedCC += CC_NUMBER_NONE;
		}
		else if (proposedCC >= CC_NUMBER_NONE) {
			proposedCC -= CC_NUMBER_NONE;
		}

		if (proposedCC == stopAt) {
			return -1;
		}
	}

	// It does always return something, above
}

Error MIDIInstrument::moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC,
                                                  ModelStackWithThreeMainThings* modelStack) {

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
	if (modelStackWithAutoParam->paramCollection == midiParamCollection) {
		midiParamCollection->params.deleteAtKey(oldCC);
	}

	// Expression param
	else {
#if ALPHA_OR_BETA_VERSION
		if (modelStackWithAutoParam->paramCollection != modelStack->paramManager->getExpressionParamSet()) {
			FREEZE_WITH_ERROR("E415");
		}
		if (modelStackWithAutoParam->paramId >= kNumExpressionDimensions) {
			FREEZE_WITH_ERROR("E416");
		}
#endif
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

int32_t MIDIInstrument::moveAutomationToDifferentCC(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode,
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

	// Need to pick a new cc which is blank on all Clips' ParamManagers with this Instrument
	// For each Clip in session and arranger for specific Output (that Output is "this")
	int32_t numElements = modelStack->song->sessionClips.getNumElements();
	bool doingArrangementClips = false;
	// TODO: Should use AllClips, but less obvious so later.
traverseClips:
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = modelStack->song->sessionClips.getClipAtIndex(c);
			if (clip->output != this) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
			clip = clipInstance->clip;
		}

		newCC = getFirstUnusedCC(modelStack, offset, newCC, *cc);
		if (newCC == -1) {
			return -1;
		}
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
	for (int32_t c = 0; c < numElements; c++) {
		Clip* clip;
		if (!doingArrangementClips) {
			clip = modelStack->song->sessionClips.getClipAtIndex(c);
			if (clip->output != this) {
				continue;
			}
		}
		else {
			ClipInstance* clipInstance = clipInstances.getElement(c);
			if (!clipInstance->clip) {
				continue;
			}
			if (!clipInstance->clip->isArrangementOnlyClip()) {
				continue;
			}
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
                                       MIDIDevice* fromDevice, bool on, int32_t receivedChannel, int32_t note,
                                       int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru) {

	if (midiInput.channelOrZone == receivedChannel) {

		// If it's a MIDI Clip, and it's outputting on the same channel as this MIDI message came in, don't do MIDI
		// thru!
		if (doingMidiThru && type == OutputType::MIDI_OUT
		    && receivedChannel == getChannel()) { // We'll just say don't do anything to midi-thru if any MPE in the
			                                      // picture, for now
			*doingMidiThru = false;
		}
	}

	NonAudioInstrument::offerReceivedNote(modelStackWithTimelineCounter, fromDevice, on, receivedChannel, note,
	                                      velocity, shouldRecordNotes, doingMidiThru);
}

void MIDIInstrument::noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) {
	int32_t channel = getChannel();
	ArpeggiatorSettings* arpSettings = NULL;
	if (activeClip) {
		arpSettings = &((InstrumentClip*)activeClip)->arpSettings;
	}

	bool arpIsOn = arpSettings != nullptr && arpSettings->mode != ArpMode::OFF;
	int32_t outputMemberChannel;

	// If no MPE, nice and simple.
	if (!sendsToMPE()) {
		outputMemberChannel = channel;
		arpNote->outputMemberChannel[noteIndex] = outputMemberChannel;
	}

	// Or if MPE, we have to decide on a member channel to assign note to...
	else {

		int32_t lowestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                                  ? 1
		                                  : MIDIDeviceManager::highestLastMemberChannelOfUpperZoneOnConnectedOutput;
		int32_t highestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                                   ? MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput
		                                   : 14;

		uint8_t numNotesPreviouslyActiveOnMemberChannel[16];
		memset(numNotesPreviouslyActiveOnMemberChannel, 0, sizeof(numNotesPreviouslyActiveOnMemberChannel));

		int32_t outputMemberChannelWithNoteSharingInputMemberChannel = 16; // 16 means "none", I think

		if (!arpIsOn) {
			// Count up notes per member channel. This traversal will *not* find the new note that we're switching on,
			// which will have had its toMIDIChannel set to MIDI_CHANNEL_NONE (255) by Arpeggiator (we'll decide and set
			// it below).
			for (int32_t n = 0; n < arpeggiator.notes.getNumElements(); n++) {
				ArpNote* thisArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				for (int32_t i = 0; i < ARP_MAX_INSTRUCTION_NOTES; i++) {
					if (thisArpNote->outputMemberChannel[i] == MIDI_CHANNEL_NONE) {
						break;
					}
					if (thisArpNote->outputMemberChannel[i] >= lowestMemberChannel
					    && thisArpNote->outputMemberChannel[i] <= highestMemberChannel) {
						numNotesPreviouslyActiveOnMemberChannel[thisArpNote->outputMemberChannel[i]]++;

						// If this note is coming in live from the same member channel as the one we wish to switch on
						// now, that's a good clue that we should group them together at the output. (Final decision to
						// be made further below.)
						if (thisArpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)]
						    == arpNote->inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)]) {
							outputMemberChannelWithNoteSharingInputMemberChannel = thisArpNote->outputMemberChannel[i];
						}
					}
				}
			}
		}

		// See which member channel fits criteria best. The MPE spec has guidelines about what criteria to use.
		uint32_t bestGoodnessFound = 0;
		bool foundOneWithRightNoteCode = false;

		for (int32_t c = lowestMemberChannel; c <= highestMemberChannel; c++) {

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

		// If we weren't able to get an output member channel all to ourselves, a better option (if it exists) would be
		// to group with note(s) which shared an input member channel
		if (numNotesPreviouslyActiveOnMemberChannel[outputMemberChannel]
		    && outputMemberChannelWithNoteSharingInputMemberChannel < 16) {
			outputMemberChannel = outputMemberChannelWithNoteSharingInputMemberChannel;
		}

		// TODO: It'd be good to be able to group them according to them having similar MPE data, which could happen if
		// they were originally recorded via the same input member channel. This would actually be really easy to do
		// reasonably well.

		// Ok. We have our new member channel.
		arpNote->outputMemberChannel[noteIndex] = outputMemberChannel;

		int16_t mpeValuesAverage[kNumExpressionDimensions]; // We may fill this up and point to it, or not
		int16_t const* mpeValuesToUse = arpNote->mpeValues;

		// If other notes are already being output on this member channel, we'll need to average MPE values and stuff
		if (numNotesPreviouslyActiveOnMemberChannel[outputMemberChannel]) {
			int32_t numNotesFound = 0;
			int32_t mpeValuesSum[kNumExpressionDimensions]; // We'll be summing 16-bit values into this 32-bit
			                                                // container, so no overflowing
			memset(mpeValuesSum, 0, sizeof(mpeValuesSum));

			for (int32_t n = 0; n < arpeggiator.notes.getNumElements();
			     n++) { // This traversal will include the original note, which will get counted up too
				ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				for (int32_t i = 0; i < ARP_MAX_INSTRUCTION_NOTES; i++) {
					if (lookingAtArpNote->outputMemberChannel[i] == outputMemberChannel) {
						numNotesFound++;
						for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
							mpeValuesSum[m] += lookingAtArpNote->mpeValues[m];
						}
					}
				}
			}

			// Work out averages
			for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
				mpeValuesAverage[m] = mpeValuesSum[m] / numNotesFound;
			}

			mpeValuesToUse = mpeValuesAverage; // And point to those averages, to actually use

			// And we'll carry on below, outputting those average values.
			// There's a chance that this new average will be the same as whatever previous ones were output on this
			// member channel. We could check for this and omit re-sending them in that case, but there's very little to
			// be gained by that added complexity.
		}

		// Ok, now we'll output MPE values - which will either be the values for this exact note we're outputting, or if
		// it's sharing a member channel, it'll be the average values we worked out above.
		outputAllMPEValuesOnMemberChannel(mpeValuesToUse, outputMemberChannel);
	}

	if (sendsToInternal()) {
		sendNoteToInternal(true, noteCodePostArp, arpNote->velocity, outputMemberChannel);
	}
	else {
		midiEngine.sendNote(this, true, noteCodePostArp, arpNote->velocity, outputMemberChannel, channel);
	}
}

// Will store them too. Only for when we definitely want to send all three.
// And obviously you can't call this unless you know that this Instrument sendsToMPE().
void MIDIInstrument::outputAllMPEValuesOnMemberChannel(int16_t const* mpeValuesToUse, int32_t outputMemberChannel) {
	int32_t channel = getChannel();
	{ // X
		int32_t outputValue14 = mpeValuesToUse[0] >> 2;
		mpeOutputMemberChannels[outputMemberChannel].lastXValueSent = outputValue14;
		int32_t outputValue14Unsigned = outputValue14 + 8192;
		midiEngine.sendPitchBend(this, outputMemberChannel, outputValue14Unsigned & 127, outputValue14Unsigned >> 7,
		                         channel);
	}

	{ // Y
		int32_t outputValue7 = mpeValuesToUse[1] >> 9;
		mpeOutputMemberChannels[outputMemberChannel].lastYAndZValuesSent[0] = outputValue7;
		midiEngine.sendCC(this, outputMemberChannel, outputMPEY, outputValue7 + 64, channel);
	}

	{ // Z
		int32_t outputValue7 = mpeValuesToUse[2] >> 8;
		mpeOutputMemberChannels[outputMemberChannel].lastYAndZValuesSent[1] = outputValue7;
		midiEngine.sendChannelAftertouch(this, outputMemberChannel, outputValue7, channel);
	}
}

void MIDIInstrument::noteOffPostArp(int32_t noteCodePostArp, int32_t oldOutputMemberChannel, int32_t velocity,
                                    int32_t noteIndex) {
	int32_t channel = getChannel();
	if (sendsToInternal()) {
		sendNoteToInternal(false, noteCodePostArp, velocity, oldOutputMemberChannel);
	}
	// If no MPE, nice and simple
	else if (!sendsToMPE()) {
		midiEngine.sendNote(this, false, noteCodePostArp, velocity, channel, kMIDIOutputFilterNoMPE);

		if (collapseAftertouch) {

			combineMPEtoMono(0, Z_PRESSURE);
		}
		// this immediately sets pitch bend and modwheel back to 0, which is not the normal MPE behaviour but
		// behaves more intuitively with multiple notes and mod wheel onto a single midi channel
		if (collapseMPE) {
			combineMPEtoMono(0, X_PITCH_BEND);
			// This is CC74 value of 0
			combineMPEtoMono(0, Y_SLIDE_TIMBRE);
		}
	}

	// Or, MPE
	else {

		mpeOutputMemberChannels[oldOutputMemberChannel].lastNoteCode = noteCodePostArp;
		mpeOutputMemberChannels[oldOutputMemberChannel].noteOffOrder = lastNoteOffOrder++;

		midiEngine.sendNote(this, false, noteCodePostArp, velocity, oldOutputMemberChannel, channel);

		// And now, if this note was sharing a member channel with any others, we want to send MPE values for those new
		// averages
		int32_t numNotesFound = 0;
		int32_t mpeValuesSum[kNumExpressionDimensions]; // We'll be summing 16-bit values into this 32-bit container, so
		                                                // no overflowing
		memset(mpeValuesSum, 0, sizeof(mpeValuesSum));

		for (int32_t n = 0; n < arpeggiator.notes.getNumElements();
		     n++) { // This traversal will not include the original note, which has already been deleted from the array.
			ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
			for (int32_t i = 0; i < ARP_MAX_INSTRUCTION_NOTES; i++) {
				if (lookingAtArpNote->outputMemberChannel[i] == oldOutputMemberChannel) {
					numNotesFound++;
					for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
						mpeValuesSum[m] += lookingAtArpNote->mpeValues[m];
					}
				}
			}
		}

		if (numNotesFound) {
			// Work out averages
			int16_t mpeValuesAverage[kNumExpressionDimensions];
			for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
				mpeValuesAverage[m] = mpeValuesSum[m] / numNotesFound;
			}
			outputAllMPEValuesOnMemberChannel(mpeValuesAverage, oldOutputMemberChannel);
		}
	}
}

void MIDIInstrument::allNotesOff() {
	int32_t channel = getChannel();
	arpeggiator.reset();

	// If no MPE, nice and simple
	if (!sendsToMPE()) {
		midiEngine.sendAllNotesOff(this, channel, kMIDIOutputFilterNoMPE);
	}

	// Otherwise, got to send message on all MPE member channels. At least I think that's right. The MPE spec talks
	// about sending "all *sounds* off" on just the master channel, but doesn't mention all *notes* off.
	else {
		// We'll send on the master channel as well as the member channels.
		int32_t lowestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                                  ? 0
		                                  : MIDIDeviceManager::highestLastMemberChannelOfUpperZoneOnConnectedOutput;
		int32_t highestMemberChannel = (channel == MIDI_CHANNEL_MPE_LOWER_ZONE)
		                                   ? MIDIDeviceManager::lowestLastMemberChannelOfLowerZoneOnConnectedOutput
		                                   : 15;

		for (int32_t c = lowestMemberChannel; c <= highestMemberChannel; c++) {
			midiEngine.sendAllNotesOff(this, c, channel);
		}
	}
}

uint8_t const shiftAmountsFrom16Bit[] = {2, 9, 8};

// The arpNote actually already contains a stored version of value32 - except it's been reduced to 16-bit, so we may as
// well use the 32-bit version here. Although, could it have even got more than 14 bits of meaningful value in the first
// place?
void MIDIInstrument::polyphonicExpressionEventPostArpeggiator(int32_t value32, int32_t noteCodeAfterArpeggiation,
                                                              int32_t whichExpressionDimension, ArpNote* arpNote,
                                                              int32_t noteIndex) {
	int32_t channel = getChannel();
	if (sendsToInternal()) {
		// Do nothing
	}
	// If we don't have MPE output...
	else if (!sendsToMPE()) {
		if (whichExpressionDimension == 2) {
			if (!collapseAftertouch) {
				// We can only send Z - and that's as polyphonic aftertouch
				midiEngine.sendPolyphonicAftertouch(this, channel, value32 >> 24, noteCodeAfterArpeggiation,
				                                    kMIDIOutputFilterNoMPE);
				return;
			}
			else {
				combineMPEtoMono(value32, whichExpressionDimension);
			}
		}
		else if (collapseMPE) {
			combineMPEtoMono(value32, whichExpressionDimension);
		}
	}

	// Or if we do have MPE output...
	else {
		int32_t memberChannel = arpNote->outputMemberChannel[noteIndex];

		// Are multiple notes sharing the same output member channel?
		ArpeggiatorSettings* settings = getArpSettings();
		if (settings == nullptr || settings->mode == ArpMode::OFF) { // Only if not arpeggiating...
			int32_t numNotesFound = 0;
			int32_t mpeValuesSum = 0; // We'll be summing 16-bit values into this 32-bit container, so no overflowing

			for (int32_t n = 0; n < arpeggiator.notes.getNumElements();
			     n++) { // This traversal will include the original note, which will get counted up too
				ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
				for (int32_t i = 0; i < ARP_MAX_INSTRUCTION_NOTES; i++) {
					if (lookingAtArpNote->outputMemberChannel[i] == memberChannel) {
						numNotesFound++;
						mpeValuesSum += lookingAtArpNote->mpeValues[whichExpressionDimension];
					}
				}
			}

			// If there in fact are multiple notes sharing the channel, to combine...
			if (numNotesFound > 1) {
				int32_t averageValue16 = mpeValuesSum / numNotesFound;

				int32_t averageValue7Or14 = averageValue16 >> shiftAmountsFrom16Bit[whichExpressionDimension];
				int32_t lastValue7Or14 =
				    whichExpressionDimension
				        ? mpeOutputMemberChannels[memberChannel].lastYAndZValuesSent[whichExpressionDimension - 1]
				        : mpeOutputMemberChannels[memberChannel].lastXValueSent;

				// If there's been no actual change, don't send anything
				if (averageValue7Or14 == lastValue7Or14) {
					return;
				}

				// Otherwise, do send this average value
				value32 = averageValue16 << 16;
			}
		}

		switch (whichExpressionDimension) {
		case 0: { // X
			int32_t value14 = (value32 >> 18);
			mpeOutputMemberChannels[memberChannel].lastXValueSent = value14;
			int32_t value14Unsigned = value14 + 8192;
			midiEngine.sendPitchBend(this, memberChannel, value14Unsigned & 127, value14Unsigned >> 7, channel);
			break;
		}

		case 1: { // Y
			int32_t value7 = value32 >> 25;
			mpeOutputMemberChannels[memberChannel].lastYAndZValuesSent[0] = value7;
			midiEngine.sendCC(this, memberChannel, outputMPEY, value7 + 64, channel);
			break;
		}

		case 2: { // Z
			int32_t value7 = value32 >> 24;
			mpeOutputMemberChannels[memberChannel].lastYAndZValuesSent[1] = value7;
			midiEngine.sendChannelAftertouch(this, memberChannel, value7, channel);
			break;
		}
		default:
			__builtin_unreachable();
		}
	}
}

void MIDIInstrument::combineMPEtoMono(int32_t value32, int32_t whichExpressionDimension) {

	ParamManager* paramManager = getParamManager(NULL);
	if (paramManager) {
		ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
		if (expressionParams) {
			cachedBendRanges[BEND_RANGE_MAIN] = expressionParams->bendRanges[BEND_RANGE_MAIN];
			cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL];
			ratio = float(cachedBendRanges[BEND_RANGE_FINGER_LEVEL]) / float(cachedBendRanges[BEND_RANGE_MAIN]);
		}
	}

	// Are multiple notes sharing the same output member channel?
	ArpeggiatorSettings* settings = getArpSettings();
	if (settings == nullptr || settings->mode == ArpMode::OFF) { // Only if not arpeggiating...
		int32_t numNotesFound = 0;
		int32_t mpeValuesSum = 0; // We'll be summing 16-bit values into this 32-bit container, so no overflowing
		int32_t mpeValuesMax = -ONE_Q31;
		// This traversal will include the original note, which will get counted up too
		for (int32_t n = 0; n < arpeggiator.notes.getNumElements(); n++) {
			ArpNote* lookingAtArpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);

			numNotesFound++;

			int32_t value = lookingAtArpNote->mpeValues[whichExpressionDimension];
			mpeValuesSum += value;
			mpeValuesMax = std::max(mpeValuesMax, value);
		}

		// If there in fact are multiple notes sharing the channel, to combine...
		if (numNotesFound >= 1) {
			int32_t averageValue16;
			if (whichExpressionDimension == 0) {
				averageValue16 = mpeValuesSum / numNotesFound;
			}
			else {
				averageValue16 = mpeValuesMax;
			}

			value32 = averageValue16 << 16;
		}
		if (whichExpressionDimension == 0) {
			// this can be bigger than 2^31
			float fbend = value32 * ratio;
			// casting down will truncate
			value32 = (int32_t)fbend;
		}
		// if it's changed, we need to update the outputs
		if (value32 != lastCombinedPolyExpression[whichExpressionDimension]) {
			lastCombinedPolyExpression[whichExpressionDimension] = value32;
			sendMonophonicExpressionEvent(whichExpressionDimension);
		}
	}
}

ModelStackWithAutoParam* MIDIInstrument::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                                int32_t paramID,
                                                                deluge::modulation::params::Kind paramKind,
                                                                bool affectEntire, bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);

	if (modelStackWithThreeMainThings) {
		ParamManager* paramManager = modelStackWithThreeMainThings->paramManager;

		if (paramManager && paramManager->containsAnyParamCollectionsIncludingExpression()) {

			modelStackWithParam = getParamToControlFromInputMIDIChannel(paramID, modelStackWithThreeMainThings);
		}
	}

	return modelStackWithParam;
}

void MIDIInstrument::sendNoteToInternal(bool on, int32_t note, uint8_t velocity, uint8_t channel) {

	switch (channel) {
	case MIDI_CHANNEL_TRANSPOSE:
		MIDITranspose::doTranspose(on, note);
	}
}

std::string_view MIDIInstrument::getNameFromCC(int32_t cc) {
	if (cc < 0 || cc >= kNumRealCCNumbers) {
		// out of range
		return std::string_view{};
	}

	MIDILabel* midiLabel = midiLabelCollection.labels.getLabelFromCC(cc);

	if (midiLabel != nullptr) {
		// found
		return std::string_view(midiLabel->name.get());
	}

	// not found
	return std::string_view{};
}

void MIDIInstrument::setNameForCC(int32_t cc, std::string_view name) {
	if (cc >= 0 && cc < kNumRealCCNumbers) {
		midiLabelCollection.labels.setOrCreateLabelForCC(cc, name.data());
	}
}
