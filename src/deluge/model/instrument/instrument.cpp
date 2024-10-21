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

#include "model/instrument/instrument.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include <cstring>
#include <new>

Instrument::Instrument(OutputType newType) : Output(newType) {
	editedByUser = false;
	existsOnCard = false;
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
	int32_t i = 0;

	while (i < clipInstances.getNumElements()) {
		ClipInstance* instance = clipInstances.getElement(i);
		if (instance->clip == clip) {
			clipInstances.deleteAtIndex(i);
		}
		else {
			i++;
		}
	}
}

bool Instrument::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {
	// midi channels are always saved, either to the midi preset or the song
	if (type == OutputType::MIDI_OUT) {
		char const* slotXMLTag = getSlotXMLTag();
		if (((MIDIInstrument*)this)->sendsToMPE()) {
			writer.writeAttribute(slotXMLTag, (((NonAudioInstrument*)this)->getChannel() == MIDI_CHANNEL_MPE_LOWER_ZONE)
			                                      ? "lower"
			                                      : "upper");
		}
		else if (((MIDIInstrument*)this)->sendsToInternal()) {
			switch (((NonAudioInstrument*)this)->getChannel()) {
			case MIDI_CHANNEL_TRANSPOSE:
				writer.writeAttribute(slotXMLTag, "transpose");
				break;
			default:
				writer.writeAttribute(slotXMLTag, ((NonAudioInstrument*)this)->getChannel());
			}
		}
		else {
			writer.writeAttribute(slotXMLTag, ((NonAudioInstrument*)this)->getChannel());
		}
		char const* subSlotTag = getSubSlotXMLTag();
		if (subSlotTag) {
			writer.writeAttribute(subSlotTag, ((MIDIInstrument*)this)->channelSuffix);
		}
	}
	// saving song
	if (!clipForSavingOutputOnly) {
		if (!name.isEmpty()) {
			writer.writeAttribute("presetName", name.get());
		}
		else if (type == OutputType::CV) {
			char const* slotXMLTag = getSlotXMLTag();

			writer.writeAttribute(slotXMLTag, ((NonAudioInstrument*)this)->getChannel());
		}
		if (!dirPath.isEmpty() && (type == OutputType::SYNTH || type == OutputType::KIT)) {
			writer.writeAttribute("presetFolder", dirPath.get());
		}
		writer.writeAttribute("defaultVelocity", defaultVelocity);
	}

	return Output::writeDataToFile(writer, clipForSavingOutputOnly, song);
}

bool Instrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	char const* slotXMLTag = getSlotXMLTag();
	char const* subSlotXMLTag = getSubSlotXMLTag();

	if (!strcmp(tagName, slotXMLTag)) {
		int32_t slotHere = reader.readTagOrAttributeValueInt();
		String slotChars;
		slotChars.setInt(slotHere, 3);
		slotChars.concatenate(&name);
		name.set(&slotChars);
	}

	else if (!strcmp(tagName, subSlotXMLTag)) {
		int32_t subSlotHere = reader.readTagOrAttributeValueInt();
		if (subSlotHere >= 0 && subSlotHere < 26) {
			char buffer[2];
			buffer[0] = 'A' + subSlotHere;
			buffer[1] = 0;
			name.concatenate(buffer);
		}
	}

	else if (!strcmp(tagName, "defaultVelocity")) {
		defaultVelocity = reader.readTagOrAttributeValueInt();
		if (defaultVelocity == 0 || defaultVelocity >= 128) {
			defaultVelocity = FlashStorage::defaultVelocity;
		}
	}

	else if (!strcmp(tagName, "presetFolder")) {
		reader.readTagOrAttributeValueString(&dirPath);
	}

	else {
		return Output::readTagFromFile(reader, tagName);
	}

	reader.exitTag();
	return true;
}

Clip* Instrument::createNewClipForArrangementRecording(ModelStack* modelStack) {

	// Allocate memory for Clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (!clipMemory) {
		return NULL;
	}

	ParamManager newParamManager;

	// For synths and kits, there'll be an existing ParamManager, and we can clone it. But for MIDI and CV, there might
	// not be, and we don't want to clone it. Instead, the call to setInstrument will create one.

	if (type == OutputType::SYNTH || type == OutputType::KIT) {

		Error error = newParamManager.cloneParamCollectionsFrom(getParamManager(modelStack->song), false, true);

		if (error != Error::NONE) {
			delugeDealloc(clipMemory);
			return NULL;
		}
	}
	else if (type == OutputType::CV) {
		if (getActiveClip()) {
			newParamManager.cloneParamCollectionsFrom(&getActiveClip()->paramManager, false,
			                                          true); // Because we want the bend ranges
		}
	}

	InstrumentClip* newInstrumentClip = new (clipMemory) InstrumentClip(modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newInstrumentClip);

	newInstrumentClip->setInstrument(this, modelStackWithTimelineCounter->song, &newParamManager);
	newInstrumentClip->setupAsNewKitClipIfNecessary(
	    modelStackWithTimelineCounter); // Fix added Sept 2020 to stop Kits from screwing up when recording in Arranger.
	                                    // Michael B discovered. Also could cause E314

	return newInstrumentClip;
}

Error Instrument::setupDefaultAudioFileDir() {
	char const* dirPathChars = dirPath.get();
	Error error =
	    audioFileManager.setupAlternateAudioFileDir(&audioFileManager.alternateAudioFileLoadPath, dirPathChars, &name);
	if (error != Error::NONE) {
		return error;
	}

	// TODO: (Kate) Why is OutputType getting converted to ThingType here???
	audioFileManager.thingBeginningLoading(static_cast<ThingType>(type));
	return Error::NONE;
}
