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
#include "deluge-project/doc_output.h" // complete DocOutput type for syncDocOutput
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include "util/string.h"
#include <cstring>
#include <new>

/*
Instrument::~Instrument() {
    // Arrangement-only Clips won't get deallocated here - that'll happen from the Song.
    // ClipInstances will all get deallocated by the vector destructor
}
*/

// M3 owner-5 Slice E: add the Instrument-tier descriptor fields to the DOM shadow (calls the Output base first).
// DO2: dir_path/default_velocity are now DOM-owned — every forward write funnels through setDirPath()/
// setDefaultVelocity() (or seedDocOutputIdentity() for the polymorphic/clone sites) instead of landing here. Nothing
// left for this method to do for them; it stays (still calling the Output base) because subclasses (NonAudio/MIDI/CV)
// chain through it for their own not-yet-inverted tiers.
void Instrument::syncDocOutput() {
	Output::syncDocOutput();
	if (docOutput_ == nullptr) {
		return;
	}
}

// DO2: reseed this tier's two identity fields from the current native values. Called (via virtual dispatch, since
// Output::seedDocOutputIdentity() is virtual) from adoptOutput's load-bridge seed and the clone/replace blocks in
// song.cpp that bulk-copy fields between Outputs without going through a setter.
void Instrument::seedDocOutputIdentity() {
	Output::seedDocOutputIdentity();
	if (docOutput_ != nullptr) {
		docOutput_->dir_path = dirPath;
		docOutput_->default_velocity = defaultVelocity;
	}
}

void Instrument::setDirPath(std::string_view newPath) {
	dirPath = newPath;
	if (docOutput_ != nullptr) {
		docOutput_->dir_path = dirPath;
	}
}
void Instrument::setDefaultVelocity(uint8_t v) {
	defaultVelocity = v;
	if (docOutput_ != nullptr) {
		docOutput_->default_velocity = v;
	}
}

// Returns whether subslot changed
void Instrument::beenEdited(bool shouldMoveToEmptySlot) {
	editedByUser = true;
}

void Instrument::deleteAnyInstancesOfClip(InstrumentClip* clip) {
	int32_t i = 0;

	while (i < std::ssize(clipInstances)) {
		ClipInstance* instance = &clipInstances[i];
		if (instance->clip == clip) {
			clipInstances.erase(clipInstances.begin() + i);
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
		// DO3: read the DOM identity record's channel when bound (rebuild-at-save); fall back to the native field
		// when unbound. Feed the record's channel into the same zone/internalDest/numeric branching (native and
		// record are always in sync when bound, since every write funnels through setChannel()).
		int32_t channelToSave = docOutput_ != nullptr ? docOutput_->channel : ((NonAudioInstrument*)this)->getChannel();
		if (((MIDIInstrument*)this)->sendsToMPE()) {
			writer.writeAttribute(slotXMLTag, (channelToSave == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "lower" : "upper");
		}
		else if (((MIDIInstrument*)this)->sendsToInternal()) {
			switch (channelToSave) {
			case MIDI_CHANNEL_TRANSPOSE:
				writer.writeAttribute(slotXMLTag, "transpose");
				break;
			default:
				writer.writeAttribute(slotXMLTag, channelToSave);
			}
		}
		else {
			writer.writeAttribute(slotXMLTag, channelToSave);
		}
		char const* subSlotTag = getSubSlotXMLTag();
		if (subSlotTag) {
			// DO3: read the DOM identity record's channel_suffix when bound; fall back to the native field.
			writer.writeAttribute(subSlotTag, docOutput_ != nullptr ? docOutput_->channel_suffix
			                                                        : ((MIDIInstrument*)this)->channelSuffix);
		}
	}
	// saving song
	if (!clipForSavingOutputOnly) {
		// DO1: read the DOM identity record's name when bound (rebuild-at-save); fall back to the native field when
		// unbound.
		bool hasPresetName = docOutput_ != nullptr ? !docOutput_->name.empty() : !name.empty();
		if (hasPresetName) {
			writer.writeAttribute("presetName", docOutput_ != nullptr ? docOutput_->name.c_str() : name.c_str());
		}
		else if (type == OutputType::CV) {
			char const* slotXMLTag = getSlotXMLTag();
			// DO3: read the DOM identity record's channel when bound; fall back to the native field.
			writer.writeAttribute(slotXMLTag, docOutput_ != nullptr ? docOutput_->channel
			                                                        : ((NonAudioInstrument*)this)->getChannel());
		}
		// DO2: read the DOM identity record's dir_path/default_velocity when bound (rebuild-at-save); fall back to the
		// native field when unbound.
		bool hasDirPath = docOutput_ != nullptr ? !docOutput_->dir_path.empty() : !dirPath.empty();
		if (hasDirPath && (type == OutputType::SYNTH || type == OutputType::KIT)) {
			writer.writeAttribute("presetFolder",
			                      docOutput_ != nullptr ? docOutput_->dir_path.c_str() : dirPath.c_str());
		}
		writer.writeAttribute("defaultVelocity",
		                      docOutput_ != nullptr ? docOutput_->default_velocity : defaultVelocity);
	}

	return Output::writeDataToFile(writer, clipForSavingOutputOnly, song);
}

bool Instrument::readTagFromFile(Deserializer& reader, char const* tagName) {

	char const* slotXMLTag = getSlotXMLTag();
	char const* subSlotXMLTag = getSubSlotXMLTag();

	if (!strcmp(tagName, slotXMLTag)) {
		int32_t slotHere = reader.readTagOrAttributeValueInt();
		std::string slotChars;
		slotChars = deluge::string::fromInt(slotHere, 3);
		slotChars.append(name);
		name = slotChars;
	}

	// getSubSlotXMLTag() returns null for instruments without a sub-slot concept (e.g. CV) - don't strcmp against null.
	else if (subSlotXMLTag && !strcmp(tagName, subSlotXMLTag)) {
		int32_t subSlotHere = reader.readTagOrAttributeValueInt();
		if (subSlotHere >= 0 && subSlotHere < 26) {
			char buffer[2];
			buffer[0] = 'A' + subSlotHere;
			buffer[1] = 0;
			name.append(buffer);
		}
	}

	else if (!strcmp(tagName, "defaultVelocity")) {
		defaultVelocity = reader.readTagOrAttributeValueInt();
		if (defaultVelocity == 0 || defaultVelocity >= 128) {
			defaultVelocity = FlashStorage::defaultVelocity;
		}
	}

	else if (!strcmp(tagName, "presetFolder")) {
		reader.readTagOrAttributeValueString(dirPath);
	}

	else {
		return Output::readTagFromFile(reader, tagName);
	}

	reader.exitTag();
	return true;
}

Clip* Instrument::createNewClipForArrangementRecording(ModelStack* modelStack) {

	// Allocate memory for Clip
	void* clipMemory = deluge::memory::alloc_fast(sizeof(InstrumentClip));
	if (!clipMemory) {
		return nullptr;
	}

	ParamManager newParamManager;

	// For synths and kits, there'll be an existing ParamManager, and we can clone it. But for MIDI and CV, there might
	// not be, and we don't want to clone it. Instead, the call to setInstrument will create one.

	if (type == OutputType::SYNTH || type == OutputType::KIT) {

		Error error = newParamManager.cloneParamCollectionsFrom(getParamManager(modelStack->song), false, true);

		if (error != Error::NONE) {
			delugeDealloc(clipMemory);
			return nullptr;
		}
	}
	else if (type == OutputType::CV) {
		if (getActiveClip()) {
			newParamManager.cloneParamCollectionsFrom(&getActiveClip()->paramManager, false,
			                                          true); // Because we want the bend ranges
		}
	}

	InstrumentClip* newInstrumentClip = new (clipMemory) InstrumentClip(*modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newInstrumentClip);

	newInstrumentClip->setInstrument(this, modelStackWithTimelineCounter->song, &newParamManager);
	newInstrumentClip->setupAsNewKitClipIfNecessary(
	    modelStackWithTimelineCounter); // Fix added Sept 2020 to stop Kits from screwing up when recording in Arranger.
	                                    // Michael B discovered. Also could cause E314

	return newInstrumentClip;
}

Error Instrument::setupDefaultAudioFileDir() {
	char const* dirPathChars = dirPath.c_str();
	auto result = audioFileManager.setupAlternateAudioFileDir(audioFileManager.alternateAudioFileLoadPath, dirPathChars,
	                                                          name.c_str());
	if (result != Error::NONE) {
		return result;
	}

	// TODO: (Kate) Why is OutputType getting converted to ThingType here???
	audioFileManager.thingBeginningLoading(static_cast<ThingType>(type));
	return Error::NONE;
}
