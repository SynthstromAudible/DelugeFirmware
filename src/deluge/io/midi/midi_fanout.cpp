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

#include "io/midi/midi_fanout.h"
#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "io/midi/midi_follow.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include <algorithm>
#include <string.h>

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_FANOUT_XML "SETTINGS/MIDIFanOut.XML"
#define MIDI_FANOUT_TAG "midiFanOut"
#define MIDI_FANOUT_GROUP_TAG "group"
#define MIDI_FANOUT_SOURCE_TAG "source"

namespace MIDIFanOut {

FanOutGroup groups[kNumGroups]{};

static bool dirty = false;

void markDirty() {
	dirty = true;
}

static void sendToDests(FanOutGroup& group, int32_t value) {
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return;
	}
	MIDIInstrument* instrument = (MIDIInstrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	RootUI* rootUI = getRootUI();
	for (FanOutDestSlot& dest : group.dests) {
		if (dest.cc == kDestCCNone || !dest.enabled) {
			continue;
		}
		// scale the incoming 0..127 value linearly onto [from, to] (from>to inverts), rounding to nearest
		int32_t span = (int32_t)dest.to - (int32_t)dest.from;
		int32_t out = dest.from + (span * value + (span >= 0 ? 63 : -63)) / 127;
		out = std::clamp<int32_t>(out, 0, kMaxValue);
		int32_t valueBig = (out - 64) << 25;
		// writes the value into the clip's own CC param, so it reaches the output and gets
		// recorded into that CC's automation lane like a manual change to it would
		instrument->processParamFromInputMIDIChannel(dest.cc, valueBig, modelStackWithTimelineCounter);
		// the param write doesn't notify the UI, so refresh the automation editor grid ourselves
		// if it's showing this CC. Can't use possiblyRefreshAutomationEditorGrid() here: for MIDI
		// clips, automation view tracks the selected lane by CC number alone and leaves
		// lastSelectedParamKind unset, so its param-kind comparison never matches
		if (rootUI == &automationView && !automationView.onArrangerView && clip->lastSelectedParamID == dest.cc) {
			uiNeedsRendering(&automationView);
		}
	}
}

bool tryFanOut(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value) {
	bool matched = false;
	for (FanOutGroup& group : groups) {
		if (!group.enabled) {
			continue;
		}
		if (group.source.equalsNoteOrCC(&cable, channelOrZone + IS_A_CC, ccNumber)) {
			sendToDests(group, value);
			matched = true;
		}
	}
	return matched;
}

// dest slot tag names are "dest1" .. "dest8"; returns the slot index, or -1 if not a dest tag
static int32_t destSlotFromTagName(char const* tagName) {
	if (!memcmp(tagName, "dest", 4) && tagName[4] >= '1' && tagName[4] < '1' + kNumDestSlots && !tagName[5]) {
		return tagName[4] - '1';
	}
	return -1;
}

void writeToFile() {
	if (!dirty) {
		return;
	}

	f_mkdir(SETTINGS_FOLDER);
	Error error = StorageManager::createXMLFile(MIDI_FANOUT_XML, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}
	Serializer& writer = GetSerializer();

	writer.writeOpeningTagBeginning(MIDI_FANOUT_TAG);
	writer.writeOpeningTagEnd();

	for (FanOutGroup& group : groups) {
		writer.writeOpeningTagBeginning(MIDI_FANOUT_GROUP_TAG);
		writer.writeAttribute("on", group.enabled ? 1 : 0, false);
		writer.writeOpeningTagEnd();

		group.source.writeCCToFile(writer, MIDI_FANOUT_SOURCE_TAG);

		for (int32_t i = 0; i < kNumDestSlots; i++) {
			FanOutDestSlot& dest = group.dests[i];
			if (dest.cc != kDestCCNone) {
				char tagName[6] = "dest1";
				tagName[4] = '1' + i;
				writer.writeOpeningTagBeginning(tagName);
				writer.writeAttribute("cc", dest.cc, false);
				writer.writeAttribute("from", dest.from, false);
				writer.writeAttribute("to", dest.to, false);
				writer.writeAttribute("on", dest.enabled ? 1 : 0, false);
				writer.closeTag();
			}
		}

		writer.writeClosingTag(MIDI_FANOUT_GROUP_TAG);
	}

	writer.writeClosingTag(MIDI_FANOUT_TAG);
	writer.closeFileAfterWriting();
	dirty = false;
}

// Reads a dest's cc/from/to/on attributes (e.g. <dest1 cc="42" from="0" to="100" on="1" />).
static void readDestFromFile(Deserializer& reader, FanOutDestSlot& dest) {
	int32_t cc = kDestCCNone;
	int32_t from = kDefaultFrom;
	int32_t to = kDefaultTo;
	bool enabled = true;
	char const* attrName;
	while (*(attrName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(attrName, "cc")) {
			cc = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(attrName, "from")) {
			from = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(attrName, "to")) {
			to = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(attrName, "on")) {
			enabled = reader.readTagOrAttributeValueInt() != 0;
		}
		reader.exitTag();
	}
	dest.cc = (cc >= 0 && cc <= kMaxDestCC) ? cc : kDestCCNone;
	dest.from = std::clamp<int32_t>(from, 0, kMaxValue);
	dest.to = std::clamp<int32_t>(to, 0, kMaxValue);
	dest.enabled = enabled;
}

static void readGroupFromFile(Deserializer& reader, FanOutGroup& group) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "on")) {
			group.enabled = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, MIDI_FANOUT_SOURCE_TAG)) {
			group.source.readCCFromFile(reader);
		}
		else {
			int32_t slot = destSlotFromTagName(tagName);
			if (slot >= 0) {
				readDestFromFile(reader, group.dests[slot]);
			}
		}
		reader.exitTag();
	}
}

void readFromFile() {
	FilePointer fp;
	if (!StorageManager::fileExists(MIDI_FANOUT_XML, &fp)) {
		return;
	}

	Error error = StorageManager::openXMLFile(&fp, smDeserializer, MIDI_FANOUT_TAG);
	if (error != Error::NONE) {
		return;
	}

	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	int32_t groupIndex = 0;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_FANOUT_GROUP_TAG) && groupIndex < kNumGroups) {
			readGroupFromFile(reader, groups[groupIndex++]);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();
}

} // namespace MIDIFanOut
