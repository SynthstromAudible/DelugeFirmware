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

#include "model/output.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_clip_existence.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"

Output::Output(OutputType newType) : type(newType) {
	mutedInArrangementMode = false;
	mutedInArrangementModeBeforeStemExport = mutedInArrangementMode;
	soloingInArrangementMode = false;
	activeClip = NULL;
	inValidState = false;
	next = NULL;
	recordingInArrangement = false;
	wasCreatedForAutoOverdub = false;
	armedForRecording = false;

	modKnobMode = 1;
}

Output::~Output() {
}

void Output::setupWithoutActiveClip(ModelStack* modelStack) {
	inValidState = true;
}

// Returns whether Clip changed from before
bool Output::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {
	if (!modelStack) {
		activeClip = nullptr;
		inValidState = false;
		return true;
	}
	Clip* newClip = (Clip*)modelStack->getTimelineCounter();

	bool clipChanged = (activeClip != newClip);
	activeClip = newClip;
	inValidState = true;
	return clipChanged;
}

void Output::detachActiveClip(Song* song) {
	activeClip = NULL;
	inValidState = false;

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

void Output::pickAnActiveClipIfPossible(ModelStack* modelStack, bool searchSessionClipsIfNeeded,
                                        PgmChangeSend maySendMIDIPGMs, bool setupWithoutActiveClipIfNeeded) {

	if (!activeClip) {

		// First, search ClipInstances in this Output.
		for (int32_t i = 0; i < clipInstances.getNumElements(); i++) {
			ClipInstance* instance = clipInstances.getElement(i);
			if (instance->clip) {
				setActiveClip(modelStack->addTimelineCounter(instance->clip), maySendMIDIPGMs);
				return;
			}
		}

		if (searchSessionClipsIfNeeded) {
			// If still here, might need to search session Clips (we've already effectively searched arrangement-only
			// Clips)
			Clip* newClip = modelStack->song->getSessionClipWithOutput(this);
			if (newClip) {
				setActiveClip(modelStack->addTimelineCounter(newClip), maySendMIDIPGMs);
				return;
			}
		}

		if (setupWithoutActiveClipIfNeeded) {
			setupWithoutActiveClip(modelStack);
		}
	}
}

void Output::pickAnActiveClipForArrangementPos(ModelStack* modelStack, int32_t arrangementPos,
                                               PgmChangeSend maySendMIDIPGMs) {

	// First, see if there's an earlier-starting ClipInstance that's still going at this pos
	int32_t i = clipInstances.search(arrangementPos + 1, LESS);
	ClipInstance* instance = clipInstances.getElement(i);
	if (instance && instance->clip && instance->pos + instance->length > arrangementPos) {
		instance->clip->activeIfNoSolo = true;
yesSetActiveClip:
		setActiveClip(modelStack->addTimelineCounter(instance->clip), maySendMIDIPGMs);
	}

	// If not, see if there's a later one we can just have
	else {
		while (true) {
			i++;
			instance = clipInstances.getElement(i);
			if (!instance) {
				break;
			}
			if (instance->clip) {
				goto yesSetActiveClip;
			}
		}

		// If still here, we didn't find anything, so try the regular pickAnActiveClipIfPossible(), which will search
		// earlier ClipInstances too, then session Clips, and failing that, a backedUpParamManager
		if (arrangementPos != 0) {
			pickAnActiveClipIfPossible(modelStack, true, maySendMIDIPGMs);
		}
	}
}

bool Output::clipHasInstance(Clip* clip) {
	for (int32_t i = 0; i < clipInstances.getNumElements(); i++) {
		ClipInstance* instance = clipInstances.getElement(i);
		if (instance->clip == clip) {
			return true;
		}
	}

	return false;
}

// check all instrument clip instances belonging to an output to see if they have any notes (for instrument clip) or
// audio file (for audio clip) if so, then we need to check if there are other clip's assigned to that output which have
// notes or audio files because we don't want to change the output type for all the clips assigned to that output if
// some have notes / audio files
bool Output::isEmpty(bool displayPopup) {
	// loop through the output selected to see if any of the clips are not empty
	for (int32_t i = 0; i < clipInstances.getNumElements(); i++) {
		Clip* clip = clipInstances.getElement(i)->clip;
		if (clip && !clip->isEmpty(displayPopup)) {
			return false;
		}
	}
	return true;
}

void Output::clipLengthChanged(Clip* clip, int32_t oldLength) {
	for (int32_t i = 0; i < clipInstances.getNumElements(); i++) {
		ClipInstance* instance = clipInstances.getElement(i);
		if (instance->clip == clip) {
			if (instance->length == oldLength) {

				int32_t newLength = clip->loopLength;

				ClipInstance* nextInstance = clipInstances.getElement(i + 1);
				if (nextInstance) {
					int32_t maxLength = nextInstance->pos - instance->pos;
					if (newLength > maxLength) {
						newLength = maxLength;
					}
				}

				instance->length = newLength;
			}
		}
	}
}

ParamManager* Output::getParamManager(Song* song) {

	if (activeClip) {
		return &activeClip->paramManager;
	}
	else {
		ParamManager* paramManager =
		    song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)toModControllable(), NULL);
		if (!paramManager) {
			FREEZE_WITH_ERROR("E170");
		}
		return paramManager;
	}
}

void Output::writeToFile(StorageManager& bdsm, Clip* clipForSavingOutputOnly, Song* song) {

	Serializer& writer = GetSerializer();
	char const* tagName = getXMLTag();
	writer.writeOpeningTagBeginning(tagName);

	if (clipForSavingOutputOnly) {
		writer.writeFirmwareVersion();
		writer.writeEarliestCompatibleFirmwareVersion("4.1.0-alpha");
	}

	bool endedOpeningTag = writeDataToFile(writer, clipForSavingOutputOnly, song);

	if (endedOpeningTag) {
		writer.writeClosingTag(tagName);
	}
	else {
		writer.closeTag();
	}
}

bool Output::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {

	if (!clipForSavingOutputOnly) {
		if (mutedInArrangementMode) {
			writer.writeAttribute("isMutedInArrangement", 1);
		}
		if (soloingInArrangementMode) {
			writer.writeAttribute("isSoloingInArrangement", 1);
		}
		writer.writeAttribute("isArmedForRecording", armedForRecording);
		writer.writeAttribute("activeModFunction", modKnobMode);

		if (clipInstances.getNumElements()) {
			writer.write("\n");
			writer.printIndents();
			writer.write("clipInstances=\"0x");

			for (int32_t i = 0; i < clipInstances.getNumElements(); i++) {
				ClipInstance* thisInstance = clipInstances.getElement(i);

				char buffer[9];

				intToHex(thisInstance->pos, buffer);
				writer.write(buffer);

				intToHex(thisInstance->length, buffer);
				writer.write(buffer);

				uint32_t clipCode;

				if (!thisInstance->clip) {
					clipCode = 0xFFFFFFFF;
				}
				else {
					clipCode = thisInstance->clip->indexForSaving;
					if (thisInstance->clip->section == 255) {
						clipCode |= (1 << 31);
					}
				}

				intToHex(clipCode, buffer);
				writer.write(buffer);
			}
			writer.write("\"");
		}

		writer.writeAttribute("colour", colour);
	}

	return false;
}

// Most classes inheriting from Output actually override this with their own version...
Error Output::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		bool readAndExited = readTagFromFile(reader, tagName);

		if (!readAndExited) {
			reader.exitTag();
		}
	}

	return Error::NONE;
}

// If this returns false, the caller has to call reader.exitTag();
bool Output::readTagFromFile(Deserializer& reader, char const* tagName) {

	if (!strcmp(tagName, "isMutedInArrangement")) {
		mutedInArrangementMode = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "isSoloingInArrangement")) {
		soloingInArrangementMode = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "isArmedForRecording")) {
		armedForRecording = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "activeModFunction")) {
		modKnobMode = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "colour")) {
		colour = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "trackInstances") || !strcmp(tagName, "clipInstances")) {

		char buffer[9];

		int32_t minPos = 0;

		int32_t numElementsToAllocateFor = 0;

		if (!reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
			goto getOut;
		}

		{
			char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
			if (!firstChars || *(uint16_t*)firstChars != charsToIntegerConstant('0', 'x')) {
				goto getOut;
			}
		}

		while (true) {

			// Every time we've reached the end of a cluster...
			if (numElementsToAllocateFor <= 0) {

				// See how many more chars before the end of the cluster. If there are any...
				uint32_t charsRemaining = reader.getNumCharsRemainingInValue();
				if (charsRemaining) {

					// Allocate space for the right number of notes, and remember how long it'll be before we need to do
					// this check again
					numElementsToAllocateFor = (uint32_t)(charsRemaining - 1) / 24 + 1;
					clipInstances.ensureEnoughSpaceAllocated(
					    numElementsToAllocateFor); // If it returns false... oh well. We'll fail later
				}
			}

			char const* hexChars = reader.readNextCharsOfTagOrAttributeValue(24);
			if (!hexChars) {
				goto getOut;
			}

			int32_t pos = hexToIntFixedLength(hexChars, 8);
			int32_t length = hexToIntFixedLength(&hexChars[8], 8);
			uint32_t clipCode = hexToIntFixedLength(&hexChars[16], 8);

			// See if that's all allowed
			if (pos < minPos || length <= 0 || pos > kMaxSequenceLength - length) {
				continue;
			}

			minPos = pos + length;

			// Ok, make the clipInstance
			int32_t i = clipInstances.insertAtKey(pos, true);
			if (i == -1) {
				return true; // Error::INSUFFICIENT_RAM;
			}
			ClipInstance* newInstance = clipInstances.getElement(i);
			newInstance->length = length;
			newInstance->clip = (Clip*)clipCode; // Sneaky - disguising int32_t as Clip*

			numElementsToAllocateFor--;
		}
	}

	else if (!strcmp(tagName, getNameXMLTag())) {
		reader.readTagOrAttributeValueString(&name);
	}

	else {
		return false;
	}

getOut:
	reader.exitTag();
	return true;
}

Error Output::possiblyBeginArrangementRecording(Song* song, int32_t newPos) {

	if (!song->arrangementOnlyClips.ensureEnoughSpaceAllocated(1)) {
		return Error::INSUFFICIENT_RAM;
	}

	if (!clipInstances.ensureEnoughSpaceAllocated(1)) {
		return Error::INSUFFICIENT_RAM;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, song);

	Clip* newClip = createNewClipForArrangementRecording(modelStack);
	if (!newClip) {
		return Error::INSUFFICIENT_RAM;
	}

	// We can only insert the ClipInstance after the call to createNewClipForArrangementRecording(), because that ends
	// up calling Song::getClipWithOutput(), which searches through ClipInstances for this Output!
	int32_t i = clipInstances.insertAtKey(newPos); // Can't fail, we checked above.
	if (i == -1) {
		return Error::INSUFFICIENT_RAM;
	}

	ClipInstance* clipInstance = clipInstances.getElement(i);

	clipInstance->clip = newClip;
	newClip->section = 255;
	newClip->loopLength = kMaxSequenceLength;

	song->arrangementOnlyClips.insertClipAtIndex(newClip, 0); // Will succeed - we checked above

	// Set the ClipInstance's length to just 1, which kinda is how long it logically "is" at this point in time before
	// recording has started. This leaves space directly after it, which the user might choose to suddenly to create
	// another ClipInstance in. Oh and also, this sets the ClipInstance's Clip to the new Clip we created, above
	clipInstance->length = 1;

	Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
	if (action) {
		action->recordClipExistenceChange(song, &song->arrangementOnlyClips, newClip, ExistenceChangeType::CREATE);
		action->recordClipInstanceExistenceChange(this, clipInstance, ExistenceChangeType::CREATE);
	}

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clipInstance->clip);

	newClip->activeIfNoSolo = true;
	newClip->setPos(modelStackWithTimelineCounter, 0, false);
	setActiveClip(modelStackWithTimelineCounter);

	newClip->beginLinearRecording(modelStackWithTimelineCounter, 0);

	recordingInArrangement = true;

	return Error::NONE;
}

void Output::endAnyArrangementRecording(Song* song, int32_t actualEndPosInternalTicks, uint32_t timeRemainder) {

	if (recordingInArrangement) {

		int32_t i = clipInstances.search(actualEndPosInternalTicks, LESS);
		ClipInstance* clipInstance = clipInstances.getElement(i);
		if (ALPHA_OR_BETA_VERSION && !clipInstance) {
			FREEZE_WITH_ERROR("E261");
		}
		if (ALPHA_OR_BETA_VERSION && clipInstance->clip != activeClip) {
			// Michael B got, in 3.2.0-alpha10. Possibly a general memory corruption thing?
			FREEZE_WITH_ERROR("E262");
		}

		int32_t lengthSoFarInternalTicks = actualEndPosInternalTicks - clipInstance->pos;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, song, activeClip);
		activeClip->finishLinearRecording(modelStack);

		activeClip->expectNoFurtherTicks(song);
		activeClip->activeIfNoSolo = false;

		uint32_t xZoom = song->xZoom[NAVIGATION_ARRANGEMENT];

		int32_t alternativeLongerLength = 0;

		bool hadToShuffleOver = false;

		// Round to nearest square at current zoom level
		int32_t quantizedEndPos = (actualEndPosInternalTicks + (xZoom >> 1)) / xZoom * xZoom;

		// But if that's taken us back to or before the clipInstance start, go forward a square
		if (quantizedEndPos <= clipInstance->pos) {
			quantizedEndPos += xZoom;
			hadToShuffleOver = true;
		}

		// But if we're not overlapping the next ClipInstance...
		ClipInstance* nextClipInstance = clipInstances.getElement(i + 1);
		if (nextClipInstance) {
			if (nextClipInstance->pos < quantizedEndPos) {

				hadToShuffleOver = true;

				// If can go back a whole square, do that
				int32_t backOneSquare = quantizedEndPos - xZoom;
				if (backOneSquare > clipInstance->pos) {
					quantizedEndPos = backOneSquare;
				}

				// Otherwise, go back just to the start of this next ClipInstance
				else {
					quantizedEndPos = nextClipInstance->pos;
				}
			}
		}

		// If we didn't have to do any extra adjusting above, and we rounded backwards, possibly give another later
		// option
		if (!hadToShuffleOver && actualEndPosInternalTicks > quantizedEndPos) {
			int32_t alternativeLaterEndPos = quantizedEndPos + xZoom;

			// But, if that'll interfere with the next clip, adjust
			if (nextClipInstance && nextClipInstance->pos < alternativeLaterEndPos) {
				alternativeLaterEndPos = nextClipInstance->pos;

				// And if that's ended up just being the same as our main option, ditch it
				if (alternativeLaterEndPos == quantizedEndPos) {
					goto skipThat;
				}
			}

			alternativeLongerLength = alternativeLaterEndPos - clipInstance->pos;
		}
skipThat: {}
		activeClip->quantizeLengthForArrangementRecording(modelStack, lengthSoFarInternalTicks, timeRemainder,
		                                                  quantizedEndPos - clipInstance->pos, alternativeLongerLength);

		// Set the ClipInstance's length to match the Clip's new length
		Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
		clipInstance->change(action, this, clipInstance->pos, activeClip->loopLength, activeClip);

		recordingInArrangement = false;
	}
}

void Output::endArrangementPlayback(Song* song, int32_t actualEndPos, uint32_t timeRemainder) {

	if (!activeClip) {
		return;
	}

	if (!recordingInArrangement) {

		// See if a ClipInstance was already playing
		int32_t i = clipInstances.search(actualEndPos, LESS);
		ClipInstance* clipInstance = clipInstances.getElement(i);
		if (clipInstance && clipInstance->clip) {
			int32_t endPos = clipInstance->pos + clipInstance->length;
			if (endPos > actualEndPos) {
				clipInstance->clip->expectNoFurtherTicks(song);
			}
		}
	}

	endAnyArrangementRecording(song, actualEndPos, timeRemainder);
}

Clip* Output::getActiveClip() const {
	return activeClip;
}

/*
for (int32_t i = 0; i < clipInstances.getNumElements(); i++) {
    ClipInstance* thisInstance = clipInstances.getElement(i);
*/
