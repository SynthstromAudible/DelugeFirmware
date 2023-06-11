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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ClipInstance.h>
#include <InstrumentClip.h>
#include <ParamManager.h>
#include <ParamManager.h>
#include "instrument.h"
#include "storagemanager.h"
#include "matrixdriver.h"
#include "lookuptables.h"
#include "string.h"
#include "uart.h"
#include "PlaybackMode.h"
#include "GeneralMemoryAllocator.h"
#include <new>
#include "FlashStorage.h"
#include "ModelStack.h"
#include "MIDIInstrument.h"
#include "functions.h"

Instrument::Instrument(int newType) : Output(newType) {
	editedByUser = false;
	existsOnCard = true;
	defaultVelocity = FlashStorage::defaultVelocity;
}

/*
Instrument::~Instrument() {
	// Arrangement-only Clips won't get deallocated here - that'll happen from the Song.
	// ClipInstances will all get deallocated by the vector destructor
}
*/

// Returns whether subslot changed
void Instrument::beenEdited(bool shouldMoveToEmptySlot) {
	editedByUser = true;
}

void Instrument::deleteAnyInstancesOfClip(InstrumentClip* clip) {
	int i = 0;

	while (i < clipInstances.getNumElements()) {
		ClipInstance* instance = clipInstances.getElement(i);
		if (instance->clip == clip) clipInstances.deleteAtIndex(i);
		else i++;
	}
}

bool Instrument::writeDataToFile(Clip* clipForSavingOutputOnly, Song* song) {

	if (!clipForSavingOutputOnly) {
		if (!name.isEmpty()) {
			storageManager.writeAttribute("presetName", name.get());
		}
		else {
			char const* slotXMLTag = getSlotXMLTag();
			if (type == INSTRUMENT_TYPE_MIDI_OUT && ((MIDIInstrument*)this)->sendsToMPE()) {
				storageManager.writeAttribute(
				    slotXMLTag,
				    (((NonAudioInstrument*)this)->channel == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "lower" : "upper");
			}
			else {
				storageManager.writeAttribute(slotXMLTag, ((NonAudioInstrument*)this)->channel);
			}
			char const* subSlotTag = getSubSlotXMLTag();
			if (subSlotTag) storageManager.writeAttribute(subSlotTag, ((MIDIInstrument*)this)->channelSuffix);
		}
		if (!dirPath.isEmpty() && (type == INSTRUMENT_TYPE_SYNTH || type == INSTRUMENT_TYPE_KIT)) {
			storageManager.writeAttribute("presetFolder", dirPath.get());
		}
		storageManager.writeAttribute("defaultVelocity", defaultVelocity);
	}

	return Output::writeDataToFile(clipForSavingOutputOnly, song);
}

bool Instrument::readTagFromFile(char const* tagName) {

	char const* slotXMLTag = getSlotXMLTag();
	char const* subSlotXMLTag = getSubSlotXMLTag();

	if (!strcmp(tagName, slotXMLTag)) {
		int slotHere = storageManager.readTagOrAttributeValueInt();
		String slotChars;
		slotChars.setInt(slotHere, 3);
		slotChars.concatenate(&name);
		name.set(&slotChars);
	}

	else if (!strcmp(tagName, subSlotXMLTag)) {
		int subSlotHere = storageManager.readTagOrAttributeValueInt();
		if (subSlotHere >= 0 && subSlotHere < 26) {
			char buffer[2];
			buffer[0] = 'A' + subSlotHere;
			buffer[1] = 0;
			name.concatenate(buffer);
		}
	}

	else if (!strcmp(tagName, "defaultVelocity")) {
		defaultVelocity = storageManager.readTagOrAttributeValueInt();
		if (defaultVelocity == 0 || defaultVelocity >= 128) defaultVelocity = FlashStorage::defaultVelocity;
	}

	else if (!strcmp(tagName, "presetFolder")) {
		storageManager.readTagOrAttributeValueString(&dirPath);
	}

	else return Output::readTagFromFile(tagName);

	storageManager.exitTag();
	return true;
}

Clip* Instrument::createNewClipForArrangementRecording(ModelStack* modelStack) {

	// Allocate memory for Clip
	void* clipMemory = generalMemoryAllocator.alloc(sizeof(InstrumentClip), NULL, false, true);
	if (!clipMemory) {
		return NULL;
	}

	ParamManager newParamManager;

	// For synths and kits, there'll be an existing ParamManager, and we can clone it. But for MIDI and CV, there might not be, and we don't want to clone it.
	// Instead, the call to setInstrument will create one.

	if (type == INSTRUMENT_TYPE_SYNTH || type == INSTRUMENT_TYPE_KIT) {

		int error = newParamManager.cloneParamCollectionsFrom(getParamManager(modelStack->song), false, true);

		if (error) {
			generalMemoryAllocator.dealloc(clipMemory);
			return NULL;
		}
	}
	else if (type == INSTRUMENT_TYPE_CV) {
		if (activeClip) {
			newParamManager.cloneParamCollectionsFrom(&activeClip->paramManager, false,
			                                          true); // Because we want the bend ranges
		}
	}

	InstrumentClip* newInstrumentClip = new (clipMemory) InstrumentClip(modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newInstrumentClip);

	newInstrumentClip->setInstrument(this, modelStackWithTimelineCounter->song, &newParamManager);
	newInstrumentClip->setupAsNewKitClipIfNecessary(
	    modelStackWithTimelineCounter); // Fix added Sept 2020 to stop Kits from screwing up when recording in Arranger. Michael B discovered. Also could cause E314

	return newInstrumentClip;
}

int Instrument::setupDefaultAudioFileDir() {
	char const* dirPathChars = dirPath.get();
	int error =
	    audioFileManager.setupAlternateAudioFileDir(&audioFileManager.alternateAudioFileLoadPath, dirPathChars, &name);
	if (error) return error;
	audioFileManager.thingBeginningLoading(type);
	return NO_ERROR;
}
