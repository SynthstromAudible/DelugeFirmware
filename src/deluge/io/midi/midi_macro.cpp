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

#include "io/midi/midi_macro.h"
#include "definitions_cxx.hpp"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "io/midi/midi_follow.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "storage/storage_manager.h"
#include <algorithm>
#include <string.h>

#define SETTINGS_FOLDER "SETTINGS"
#define MIDI_MACRO_XML "SETTINGS/MIDIMacro.XML"
#define MIDI_MACRO_FILE_TAG "midiMacro" // root element
#define MIDI_MACRO_ELEMENT_TAG "macro"  // one macro (leader + followers)
#define MIDI_MACRO_LEADER_TAG "leader"  // the learned leader CC
#define MIDI_MACRO_FOLLOWER_PREFIX "follower"

namespace MIDIMacro {

Macro macros[kNumMacros]{};

int32_t presetMacroIndex = 0;

static bool dirty = false;

void markDirty() {
	dirty = true;
}

bool isEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::MidiMacro) == RuntimeFeatureStateToggle::On;
}

static void sendToFollowers(Macro& macro, int32_t value) {
	// Don't drive followers while a song is loading. On boot a controller's power-on CC burst (e.g. the
	// MIDI Fighter Twister resending its knob positions) can arrive mid-load of the startup song,
	// when the active clip/song isn't in a stable state yet - touching it then crashes. Also covers
	// loading a different song at runtime.
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return;
	}
	MIDIInstrument* instrument = (MIDIInstrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	RootUI* rootUI = getRootUI();
	for (MacroFollowerSlot& follower : macro.followers) {
		if (follower.cc == kFollowerCCNone || !follower.enabled) {
			continue;
		}
		// scale the incoming 0..127 value linearly onto [from, to] (from>to inverts), rounding to nearest
		int32_t span = (int32_t)follower.to - (int32_t)follower.from;
		int32_t out = follower.from + (span * value + (span >= 0 ? 63 : -63)) / 127;
		out = std::clamp<int32_t>(out, 0, kMaxValue);
		int32_t valueBig = (out - 64) << 25;
		// writes the value into the clip's own CC param, so it reaches the output and gets
		// recorded into that CC's automation lane like a manual change to it would
		instrument->processParamFromInputMIDIChannel(follower.cc, valueBig, modelStackWithTimelineCounter);
		// the param write doesn't notify the UI, so refresh the automation editor grid ourselves
		// if it's showing this CC. Can't use possiblyRefreshAutomationEditorGrid() here: for MIDI
		// clips, automation view tracks the selected lane by CC number alone and leaves
		// lastSelectedParamKind unset, so its param-kind comparison never matches
		if (rootUI == &automationView && !automationView.onArrangerView && clip->lastSelectedParamID == follower.cc) {
			uiNeedsRendering(&automationView);
		}
	}
}

bool tryMacro(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value) {
	if (!isEnabled()) {
		return false;
	}
	bool matched = false;
	for (Macro& macro : macros) {
		if (!macro.enabled) {
			continue;
		}
		if (macro.leader.equalsNoteOrCC(&cable, channelOrZone + IS_A_CC, ccNumber)) {
			sendToFollowers(macro, value);
			matched = true;
		}
	}
	return matched;
}

// follower slot tag names are "follower1" .. "follower8"; returns the slot index, or -1 if not one.
static int32_t followerSlotFromTagName(char const* tagName) {
	constexpr int32_t prefixLen = 8; // strlen("follower")
	if (!memcmp(tagName, MIDI_MACRO_FOLLOWER_PREFIX, prefixLen) && tagName[prefixLen] >= '1'
	    && tagName[prefixLen] < '1' + kNumFollowerSlots && !tagName[prefixLen + 1]) {
		return tagName[prefixLen] - '1';
	}
	return -1;
}

// Writes one <macro> element (self-closing if empty), as it appears in both the main file and a
// single-macro preset file.
static void writeMacroToFile(Serializer& writer, Macro& macro) {
	// Does this macro have any children to write? (A learned leader or at least one configured follower.)
	bool hasChildren = macro.leader.containsSomething();
	for (int32_t i = 0; i < kNumFollowerSlots && !hasChildren; i++) {
		hasChildren = macro.followers[i].cc != kFollowerCCNone;
	}

	writer.writeOpeningTagBeginning(MIDI_MACRO_ELEMENT_TAG);
	writer.writeAttribute("enabled", macro.enabled ? 1 : 0, false);

	// Self-close empty macros (<macro enabled="0"/>). The XML reader mis-parses an empty but
	// explicitly-closed element that carries an attribute (<macro enabled="0"></macro>), which
	// desyncs the rest of the file; a self-closing tag avoids that. Macros must stay positional
	// (slot 0..3 = macro index), so we always write all of them rather than skipping empties.
	if (!hasChildren) {
		writer.closeTag();
		return;
	}

	writer.writeOpeningTagEnd();

	macro.leader.writeCCToFile(writer, MIDI_MACRO_LEADER_TAG);

	for (int32_t i = 0; i < kNumFollowerSlots; i++) {
		MacroFollowerSlot& follower = macro.followers[i];
		if (follower.cc != kFollowerCCNone) {
			char tagName[10] = MIDI_MACRO_FOLLOWER_PREFIX "1"; // "follower1"
			tagName[8] = '1' + i;
			writer.writeOpeningTagBeginning(tagName);
			writer.writeAttribute("cc", follower.cc, false);
			writer.writeAttribute("from", follower.from, false);
			writer.writeAttribute("to", follower.to, false);
			writer.writeAttribute("send", follower.enabled ? 1 : 0, false);
			writer.closeTag();
		}
	}

	writer.writeClosingTag(MIDI_MACRO_ELEMENT_TAG);
}

void writeToFile() {
	if (!dirty) {
		return;
	}

	f_mkdir(SETTINGS_FOLDER);
	Error error = StorageManager::createXMLFile(MIDI_MACRO_XML, smSerializer, true);
	if (error != Error::NONE) {
		return;
	}
	Serializer& writer = GetSerializer();

	writer.writeOpeningTagBeginning(MIDI_MACRO_FILE_TAG);
	writer.writeOpeningTagEnd();

	for (Macro& macro : macros) {
		writeMacroToFile(writer, macro);
	}

	writer.writeClosingTag(MIDI_MACRO_FILE_TAG);
	writer.closeFileAfterWriting();
	dirty = false;
}

// Reads a follower's cc/from/to/send attributes (e.g. <follower1 cc="42" from="0" to="100" send="1" />).
static void readFollowerFromFile(Deserializer& reader, MacroFollowerSlot& follower) {
	int32_t cc = kFollowerCCNone;
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
		else if (!strcmp(attrName, "send")) {
			enabled = reader.readTagOrAttributeValueInt() != 0;
		}
		reader.exitTag();
	}
	follower.cc = (cc >= 0 && cc <= kMaxFollowerCC) ? cc : kFollowerCCNone;
	follower.from = std::clamp<int32_t>(from, 0, kMaxValue);
	follower.to = std::clamp<int32_t>(to, 0, kMaxValue);
	follower.enabled = enabled;
}

static void readMacroFromFile(Deserializer& reader, Macro& macro) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "enabled")) {
			macro.enabled = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, MIDI_MACRO_LEADER_TAG)) {
			macro.leader.readCCFromFile(reader);
		}
		else {
			int32_t slot = followerSlotFromTagName(tagName);
			if (slot >= 0) {
				readFollowerFromFile(reader, macro.followers[slot]);
			}
		}
		reader.exitTag();
	}
}

void readFromFile() {
	FilePointer fp;
	if (!StorageManager::fileExists(MIDI_MACRO_XML, &fp)) {
		return;
	}

	Error error = StorageManager::openXMLFile(&fp, smDeserializer, MIDI_MACRO_FILE_TAG);
	if (error != Error::NONE) {
		return;
	}

	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	int32_t macroIndex = 0;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_MACRO_ELEMENT_TAG) && macroIndex < kNumMacros) {
			readMacroFromFile(reader, macros[macroIndex++]);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();
}

// Writes a one-macro preset: the same <midiMacro> wrapper as the main file, holding a single
// <macro>. The caller (the save browser) owns createXMLFile()/closeFileAfterWriting().
void writeMacroPreset(Serializer& writer, int32_t macroIndex) {
	writer.writeOpeningTagBeginning(MIDI_MACRO_FILE_TAG);
	writer.writeOpeningTagEnd();
	writeMacroToFile(writer, macros[macroIndex]);
	writer.writeClosingTag(MIDI_MACRO_FILE_TAG);
}

// Loads the first <macro> from a preset file into macros[macroIndex], replacing it wholesale.
Error loadMacroPreset(FilePointer* fp, int32_t macroIndex) {
	Error error = StorageManager::openXMLFile(fp, smDeserializer, MIDI_MACRO_FILE_TAG);
	if (error != Error::NONE) {
		return error;
	}

	// Reset the target macro first so anything not present in the preset (e.g. follower slots the
	// preset leaves unconfigured) is cleared rather than left over from the previous contents.
	macros[macroIndex] = Macro{};

	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MIDI_MACRO_ELEMENT_TAG)) {
			readMacroFromFile(reader, macros[macroIndex]);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();

	// Persist the loaded state into the main MIDIMacro.XML too.
	markDirty();
	return Error::NONE;
}

void capture(int32_t macroIndex, bool toMax) {
	// Same guards/setup as sendToFollowers(): need a stable active MIDI clip.
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return;
	}
	MIDIInstrument* instrument = (MIDIInstrument*)clip->output;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	bool changed = false;
	for (MacroFollowerSlot& follower : macros[macroIndex].followers) {
		if (follower.cc == kFollowerCCNone) {
			continue;
		}
		// Read this CC's current live value - the inverse of the write in sendToFollowers().
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStackWithTimelineCounter->addNoteRow(0, nullptr)
		        ->addOtherTwoThings(instrument->toModControllable(), instrument->getParamManager(currentSong));
		ModelStackWithAutoParam* modelStackWithParam =
		    instrument->getParamToControlFromInputMIDIChannel(follower.cc, modelStackWithThreeMainThings);
		if (!modelStackWithParam->autoParam) {
			continue;
		}
		int32_t paramValue = modelStackWithParam->autoParam->getCurrentValue();
		int32_t value = std::clamp<int32_t>(((paramValue + (1 << 24)) >> 25) + 64, 0, kMaxValue);
		(toMax ? follower.to : follower.from) = value;
		changed = true;
	}
	if (changed) {
		markDirty();
	}
}

} // namespace MIDIMacro
