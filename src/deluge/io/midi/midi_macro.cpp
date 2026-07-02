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
#include "gui/l10n/l10n.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "io/midi/midi_follow.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/midi/midi_param_vector.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "storage/storage_manager.h"
#include "util/d_stringbuf.h"
#include "util/misc.h"
#include <algorithm>
#include <string.h>

#define MIDI_MACROS_TAG "midiMacros"      // per-instrument block, embedded in the instrument's XML
#define MIDI_MACRO_PRESET_TAG "midiMacro" // preset file root (followers only)
#define MIDI_MACRO_ELEMENT_TAG "macro"    // one macro (leader + followers)
#define MIDI_MACRO_LEADER_TAG "leader"    // the learned leader CC
#define MIDI_MACRO_FOLLOWER_PREFIX "follower"

namespace MIDIMacro {

int32_t presetMacroIndex = 0;

bool isEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::MidiMacro) == RuntimeFeatureStateToggle::On;
}

int32_t findFollowerCCOwner(const Macro* macros, uint8_t cc, int32_t exceptMacro, int32_t exceptSlot,
                            int32_t* slotOut) {
	if (cc == kFollowerCCNone) {
		return -1;
	}
	for (int32_t m = 0; m < kNumMacros; m++) {
		for (int32_t f = 0; f < kNumFollowerSlots; f++) {
			if (m == exceptMacro && f == exceptSlot) {
				continue;
			}
			if (macros[m].followers[f].cc == cc) {
				if (slotOut != nullptr) {
					*slotOut = f;
				}
				return m;
			}
		}
	}
	return -1;
}

int32_t findShadowingOwner(const Macro* macros, uint8_t cc, int32_t macroIndex, int32_t slot) {
	if (cc == kFollowerCCNone) {
		return -1;
	}
	// Scan in ownership order (macros, then slots); anything at or after our own position can't shadow us.
	for (int32_t m = 0; m < kNumMacros; m++) {
		for (int32_t f = 0; f < kNumFollowerSlots; f++) {
			if (m == macroIndex && f == slot) {
				return -1;
			}
			if (macros[m].followers[f].cc == cc) {
				return m;
			}
		}
	}
	return -1;
}

// Appends a destination CC's label: the active MIDI instrument's device name if loaded, else "CC <n>".
static void appendDestName(StringBuf& buf, uint8_t cc) {
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	std::string_view name{};
	if (clip && clip->output && clip->output->type == OutputType::MIDI_OUT) {
		name = ((MIDIInstrument*)clip->output)->getNameFromCC(cc);
	}
	if (!name.empty()) {
		buf.append(name);
	}
	else {
		buf.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_FOLLOWER_CC)); // "CC"
		buf.append(' ');
		buf.appendInt(cc);
	}
}

// Appends "<dest> used by\nMacro N" (OLED) / "<dest> used by Macro N" (7SEG).
static void appendCCUsedBy(StringBuf& buf, uint8_t cc, int32_t ownerMacro) {
	appendDestName(buf, cc);
	buf.append(' ');
	buf.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_USED_BY)); // "used by"
	buf.append(display->haveOLED() ? '\n' : ' ');
	auto macroName =
	    static_cast<deluge::l10n::String>(util::to_underlying(deluge::l10n::String::STRING_FOR_MACRO_1) + ownerMacro);
	buf.append(deluge::l10n::get(macroName)); // "Macro N"
}

void showCCConflictPopup(uint8_t cc, int32_t ownerMacro, bool persistent) {
	DEF_STACK_STRING_BUF(popup, 48);
	appendCCUsedBy(popup, cc, ownerMacro); // e.g. "LFO Freq used by" / "Macro 1"
	if (persistent) {
		display->popupText(popup.c_str());
	}
	else {
		display->displayPopup(popup.c_str());
	}
}

bool showMacroInactivePopup(int32_t macroIndex) {
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return false;
	}
	MIDIInstrument* instrument = (MIDIInstrument*)clip->output;
	if (instrument->macros[macroIndex].active) {
		return false; // active -> nothing to explain
	}
	uint8_t conflictCC = 0;
	int32_t owner = findActiveConflict(instrument->macros, macroIndex, &conflictCC);
	if (owner < 0) {
		return false; // inactive but activatable -> no conflict to report
	}
	// "Inactive" / "<dest> used by" / "Macro N"
	DEF_STACK_STRING_BUF(popup, 56);
	popup.append("Inactive");
	popup.append(display->haveOLED() ? '\n' : ' ');
	appendCCUsedBy(popup, conflictCC, owner);
	display->displayPopup(popup.c_str());
	return true;
}

int32_t findActiveConflict(const Macro* macros, int32_t macroIndex, uint8_t* conflictCC) {
	for (const MacroFollowerSlot& follower : macros[macroIndex].followers) {
		if (follower.cc == kFollowerCCNone) {
			continue;
		}
		for (int32_t m = 0; m < kNumMacros; m++) {
			if (m == macroIndex || !macros[m].active) {
				continue;
			}
			for (const MacroFollowerSlot& other : macros[m].followers) {
				if (other.cc == follower.cc) {
					if (conflictCC != nullptr) {
						*conflictCC = follower.cc;
					}
					return m;
				}
			}
		}
	}
	return -1;
}

bool anyMacroConfigured(Macro* macros) {
	for (int32_t m = 0; m < kNumMacros; m++) {
		if (macros[m].active || macros[m].leader.containsSomething() || macros[m].leaderKnob >= 0) {
			return true;
		}
		for (const MacroFollowerSlot& follower : macros[m].followers) {
			if (follower.cc != kFollowerCCNone) {
				return true;
			}
		}
	}
	return false;
}

// Scale a leader value 0..127 linearly onto follower [from, to] (from>to inverts), rounding to nearest.
static inline int32_t scaleFollower(const MacroFollowerSlot& follower, int32_t value) {
	int32_t span = (int32_t)follower.to - (int32_t)follower.from;
	int32_t out = follower.from + (span * value + (span >= 0 ? 63 : -63)) / 127;
	return std::clamp<int32_t>(out, 0, kMaxValue);
}

// Saturating conversion from a big param value to a 0..127 CC value. Reuses the collection's guarded
// rounding: a maxed automation node holds INT32_MAX (knobPosToParamValue special case), which the
// naive (v + (1<<24)) >> 25 would wrap negative and decode as 0 instead of 127.
static inline int32_t bigValueToCC(int32_t value) {
	return std::clamp<int32_t>(MIDIParamCollection::autoparamValueToCC(value) + 64, 0, (int32_t)kMaxValue);
}

// Validates the clip is a MIDI clip and returns its instrument, else null.
static MIDIInstrument* midiClipInstrument(Clip* clip) {
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return nullptr;
	}
	return (MIDIInstrument*)clip->output;
}

// Defined with the fan-out machinery below.
static MIDIParamCollection* getMIDIParams(Clip* clip, ParamCollectionSummary** summaryOut);
static void refreshAutomationGridIfShowingCC(Clip* clip, uint8_t cc);

static void sendToFollowers(MIDIInstrument* instrument, Clip* clip, ModelStackWithTimelineCounter* modelStack,
                            Macro& macro, int32_t value) {
	RootUI* rootUI = getRootUI();
	int32_t macroIndex = (int32_t)(&macro - instrument->macros);
	for (int32_t slot = 0; slot < kNumFollowerSlots; slot++) {
		MacroFollowerSlot& follower = macro.followers[slot];
		if (follower.cc == kFollowerCCNone || !follower.send) {
			continue;
		}
		// A shadowed follower (its CC is owned by an earlier follower) is inert - only the owner drives.
		if (isFollowerShadowed(instrument->macros, macroIndex, slot)) {
			continue;
		}
		int32_t valueBig = (scaleFollower(follower, value) - 64) << 25;
		// writes the value into the clip's own CC param, so it reaches the output and gets
		// recorded into that CC's automation lane like a manual change to it would
		instrument->processParamFromInputMIDIChannel(follower.cc, valueBig, modelStack);
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
	// Don't drive followers while a song is loading. On boot a controller's power-on CC burst (e.g. the
	// MIDI Fighter Twister resending its knob positions) can arrive mid-load of the startup song, when
	// the active clip/song isn't in a stable state yet - touching it then crashes. Also covers loading a
	// different song at runtime.
	if (getCurrentUI() == &loadSongUI) {
		return false;
	}
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return false;
	}
	MIDIInstrument* instrument = (MIDIInstrument*)clip->output;
	if (!instrument->macrosEnabled) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	bool matched = false;
	for (Macro& macro : instrument->macros) {
		if (!macro.active) {
			continue;
		}
		if (macro.leader.equalsNoteOrCC(&cable, channelOrZone + IS_A_CC, ccNumber)) {
			sendToFollowers(instrument, clip, modelStackWithTimelineCounter, macro, value);
			matched = true;
		}
	}
	return matched;
}

bool tryKnobMacro(int32_t whichKnob, int32_t offset) {
	if (!isEnabled() || getCurrentUI() == &loadSongUI) {
		return false;
	}
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	if (!clip || !clip->output || clip->output->type != OutputType::MIDI_OUT) {
		return false;
	}
	MIDIInstrument* instrument = (MIDIInstrument*)clip->output;
	if (!instrument->macrosEnabled) {
		return false;
	}
	// Cheap pre-check so a normal (non-leader) knob turn bails before building a modelStack.
	bool anyMatch = false;
	for (Macro& macro : instrument->macros) {
		if (macro.active && macro.leaderKnob == whichKnob) {
			anyMatch = true;
			break;
		}
	}
	if (!anyMatch) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	for (Macro& macro : instrument->macros) {
		if (!macro.active || macro.leaderKnob != whichKnob) {
			continue;
		}
		// A gold-knob leader is a relative encoder: accumulate its position, then drive the followers.
		int32_t pos = std::clamp<int32_t>((int32_t)macro.leaderKnobPos + offset, 0, kMaxValue);
		macro.leaderKnobPos = pos;
		sendToFollowers(instrument, clip, modelStackWithTimelineCounter, macro, pos);
	}
	return true;
}

bool tryModeKnobMacro(int32_t whichKnob, int32_t offset) {
	if (whichKnob < 0 || whichKnob >= kNumMacros) {
		return false;
	}
	if (!isEnabled() || getCurrentUI() == &loadSongUI) {
		return false;
	}
	Clip* clip = midiFollow.getSelectedOrActiveClip();
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument || !instrument->macrosEnabled) {
		return false;
	}
	Macro& macro = instrument->macros[whichKnob]; // knob 1 -> Macro 1, knob 2 -> Macro 2
	if (!macro.active) {
		return false; // inactive macro: let the knob keep its normal behavior
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	int32_t pos = std::clamp<int32_t>((int32_t)macro.leaderKnobPos + offset, 0, kMaxValue);
	macro.leaderKnobPos = pos;
	sendToFollowers(instrument, clip, modelStackWithTimelineCounter, macro, pos);
	return true;
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

// Writes the configured <follower1..8/> elements of a macro. Used by both the instrument block and presets.
static void writeFollowersToFile(Serializer& writer, Macro& macro) {
	for (int32_t i = 0; i < kNumFollowerSlots; i++) {
		MacroFollowerSlot& follower = macro.followers[i];
		if (follower.cc != kFollowerCCNone) {
			char tagName[10] = MIDI_MACRO_FOLLOWER_PREFIX "1"; // "follower1"
			tagName[8] = '1' + i;
			writer.writeOpeningTagBeginning(tagName);
			writer.writeAttribute("cc", follower.cc, false);
			writer.writeAttribute("from", follower.from, false);
			writer.writeAttribute("to", follower.to, false);
			writer.writeAttribute("send", follower.send ? 1 : 0, false);
			writer.closeTag();
		}
	}
}

// Writes one <macro> element (self-closing if empty), as it appears in the instrument block.
static void writeMacroToFile(Serializer& writer, Macro& macro) {
	// Does this macro have any children to write? (A learned leader or at least one configured follower.)
	bool hasChildren = macro.leader.containsSomething();
	for (int32_t i = 0; i < kNumFollowerSlots && !hasChildren; i++) {
		hasChildren = macro.followers[i].cc != kFollowerCCNone;
	}

	writer.writeOpeningTagBeginning(MIDI_MACRO_ELEMENT_TAG);
	writer.writeAttribute("active", macro.active ? 1 : 0, false);
	if (macro.leaderKnob >= 0) {
		writer.writeAttribute("leaderKnob", macro.leaderKnob, false); // gold-knob leader rides on the tag
	}

	// Self-close empty macros (<macro active="0"/>). The XML reader mis-parses an empty but
	// explicitly-closed element that carries an attribute (<macro active="0"></macro>), which
	// desyncs the rest of the file; a self-closing tag avoids that. Macros must stay positional
	// (slot 0..3 = macro index), so we always write all of them rather than skipping empties.
	if (!hasChildren) {
		writer.closeTag();
		return;
	}

	writer.writeOpeningTagEnd();

	macro.leader.writeCCToFile(writer, MIDI_MACRO_LEADER_TAG);

	writeFollowersToFile(writer, macro);

	writer.writeClosingTag(MIDI_MACRO_ELEMENT_TAG);
}

// Writes the <midiMacros enabled=".."> block holding all four macros. Called from
// MIDIInstrument::writeDataToFile with the instrument's own macros/enable state.
void writeMacrosToFile(Serializer& writer, Macro* macros, bool enabled) {
	writer.writeOpeningTagBeginning(MIDI_MACROS_TAG);
	writer.writeAttribute("enabled", enabled ? 1 : 0, false); // per-instrument enable gate
	writer.writeOpeningTagEnd();

	for (int32_t m = 0; m < kNumMacros; m++) {
		writeMacroToFile(writer, macros[m]);
	}

	writer.writeClosingTag(MIDI_MACROS_TAG);
}

// Reads a follower's cc/from/to/send attributes (e.g. <follower1 cc="42" from="0" to="100" send="1" />).
static void readFollowerFromFile(Deserializer& reader, MacroFollowerSlot& follower) {
	int32_t cc = kFollowerCCNone;
	int32_t from = kDefaultFrom;
	int32_t to = kDefaultTo;
	bool send = true;
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
			send = reader.readTagOrAttributeValueInt() != 0;
		}
		reader.exitTag();
	}
	follower.cc = (cc >= 0 && cc <= kMaxFollowerCC) ? cc : kFollowerCCNone;
	follower.from = std::clamp<int32_t>(from, 0, kMaxValue);
	follower.to = std::clamp<int32_t>(to, 0, kMaxValue);
	follower.send = send;
}

static void readMacroFromFile(Deserializer& reader, Macro& macro) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// accept legacy "enabled" as well as "active"
		if (!strcmp(tagName, "active") || !strcmp(tagName, "enabled")) {
			macro.active = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, "leaderKnob")) {
			int32_t k = reader.readTagOrAttributeValueInt();
			macro.leaderKnob = (k >= 0 && k < kNumPhysicalModKnobs) ? (int8_t)k : -1;
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

// Reads the children of a <midiMacros> block: the per-instrument enable attribute and the four
// positional <macro> elements. The caller (MIDIInstrument::readTagFromFile) exits the outer tag.
void readMacrosFromFile(Deserializer& reader, Macro* macros, bool& enabled) {
	char const* tagName;
	int32_t macroIndex = 0;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "enabled")) {
			enabled = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, MIDI_MACRO_ELEMENT_TAG) && macroIndex < kNumMacros) {
			readMacroFromFile(reader, macros[macroIndex++]);
		}
		reader.exitTag();
	}
}

// Writes a preset: followers only (<midiMacro><follower1..8/></midiMacro>). A preset is a portable
// follower configuration - it deliberately omits the leader and the macro's active state so it can
// be applied to any macro. The caller (save browser) owns createXMLFile()/closeFileAfterWriting().
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex) {
	writer.writeOpeningTagBeginning(MIDI_MACRO_PRESET_TAG);
	writer.writeOpeningTagEnd();
	writeFollowersToFile(writer, macros[macroIndex]);
	writer.writeClosingTag(MIDI_MACRO_PRESET_TAG);
}

// Loads a preset's followers into macros[macroIndex], replacing that macro's followers only and
// leaving its leader and active state untouched. If the loaded followers conflict with an active
// macro, the loaded macro is deactivated (its CCs are kept), since destination CCs must be unique.
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex) {
	Error error = StorageManager::openXMLFile(fp, smDeserializer, MIDI_MACRO_PRESET_TAG);
	if (error != Error::NONE) {
		return error;
	}

	// Remember which CCs the old follower set targeted, so their baked lanes can be cleared if the
	// preset no longer uses them (otherwise they'd keep playing as ghost lanes).
	uint8_t oldCCs[kNumFollowerSlots];
	for (int32_t f = 0; f < kNumFollowerSlots; f++) {
		oldCCs[f] = macros[macroIndex].followers[f].cc;
	}

	// Reset only the followers so slots the preset doesn't configure are cleared, not left over.
	for (MacroFollowerSlot& follower : macros[macroIndex].followers) {
		follower = MacroFollowerSlot{};
	}

	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		int32_t slot = followerSlotFromTagName(tagName);
		if (slot >= 0) {
			readFollowerFromFile(reader, macros[macroIndex].followers[slot]);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();

	if (macros[macroIndex].active && macroHasActiveConflict(macros, macroIndex)) {
		macros[macroIndex].active = false;
	}

	// Clear baked lanes on CCs the preset dropped, then re-bake the macro lane into the new set.
	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = (clip != nullptr) ? getMIDIParams(clip, &summary) : nullptr;
	if (coll && coll->params.getParamFromCC(paramIDForMacro(macroIndex)) && midiClipInstrument(clip)) {
		MIDIInstrument* instrument = midiClipInstrument(clip);
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		ModelStackWithThreeMainThings* three =
		    modelStack->addTimelineCounter(clip)
		        ->addNoteRow(0, nullptr)
		        ->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);
		for (int32_t f = 0; f < kNumFollowerSlots; f++) {
			uint8_t oldCC = oldCCs[f];
			if (oldCC == kFollowerCCNone || findFollowerCCOwner(macros, oldCC, -1, -1) >= 0) {
				continue; // dropped CC still targeted by some follower - its bake stays
			}
			MIDIParam* oldParam = coll->params.getParamFromCC(oldCC);
			if (oldParam) {
				ModelStackWithAutoParam* modelStackWithParam =
				    three->addParamCollectionAndId(coll, summary, oldCC)->addAutoParam(&oldParam->param);
				oldParam->param.deleteAutomation(nullptr, modelStackWithParam, false);
				refreshAutomationGridIfShowingCC(clip, oldCC);
			}
		}
	}
	reFanMacro(clip, macroIndex, nullptr); // new follower set -> re-bake the macro lane into it
	view.setModLedStates();                // refresh the follower-assignment LEDs

	return Error::NONE;
}

void capture(int32_t macroIndex, bool toMax) {
	// Same guards/setup as tryMacro(): need a stable active MIDI clip.
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
	for (MacroFollowerSlot& follower : instrument->macros[macroIndex].followers) {
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
		int32_t value = bigValueToCC(modelStackWithParam->autoParam->getCurrentValue());
		(toMax ? follower.to : follower.from) = value;
		changed = true;
	}
	if (changed) {
		instrument->editedByUser = true;
	}
}

// The clip's own MIDI param collection - where both real-CC and macro-lane params live. Baking always
// targets the edited clip's params, NOT instrument->getParamManager() (that's the ACTIVE clip's, which
// can be a different clip on the same track).
static MIDIParamCollection* getMIDIParams(Clip* clip, ParamCollectionSummary** summaryOut) {
	ParamCollectionSummary* summary = clip->paramManager.getMIDIParamCollectionSummary();
	if (!summary || !summary->paramCollection) {
		return nullptr;
	}
	*summaryOut = summary;
	return (MIDIParamCollection*)summary->paramCollection;
}

// Clears one follower's lane and, when sending, mirrors the leader lane into it scaled by [from, to].
// The caller must not create any params between resolving `leader` and this call: an insert moves the
// vector's storage and would dangle the pointer.
static void bakeFollower(MIDIParamCollection* coll, ParamCollectionSummary* summary,
                         ModelStackWithThreeMainThings* three, const MacroFollowerSlot& follower, AutoParam* leader,
                         Action* action) {
	MIDIParam* followerParam = coll->params.getParamFromCC(follower.cc);
	if (!followerParam) {
		return; // never existed: nothing to clear, and nothing will be written (send off)
	}
	ModelStackWithAutoParam* modelStackWithParam =
	    three->addParamCollectionAndId(coll, summary, follower.cc)->addAutoParam(&followerParam->param);

	// Overwrite: clear first (deleteAutomation snapshots into `action` for undo when given).
	followerParam->param.deleteAutomation(action, modelStackWithParam, false);

	if (follower.send) {
		for (int32_t n = 0; n < leader->nodes.getNumElements(); n++) {
			ParamNode* node = leader->nodes.getElement(n);
			int32_t out = scaleFollower(follower, bigValueToCC(node->value));
			followerParam->param.setNodeAtPos(node->pos, (out - 64) << 25, node->interpolated);
		}
		// Base value for regions before the first node / when the leader has no nodes.
		int32_t base = scaleFollower(follower, bigValueToCC(leader->getCurrentValue()));
		followerParam->param.setCurrentValueBasicForSetup((base - 64) << 25);
	}
}

static void refreshAutomationGridIfShowingCC(Clip* clip, uint8_t cc) {
	if (getRootUI() == &automationView && !automationView.onArrangerView && clip->lastSelectedParamID == cc) {
		uiNeedsRendering(&automationView);
	}
}

// Mirror macroIndex's leader-lane automation (pseudo-CC paramIDForMacro) into each follower CC's
// automation on `clip`, scaled. Overwrites the follower lanes; snapshots them into `action` first.
static void fanOutMacroLane(MIDIInstrument* instrument, Clip* clip, int32_t macroIndex,
                            ModelStackWithTimelineCounter* modelStack, Action* action) {
	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll) {
		return;
	}
	// If the macro lane's param was never created on this clip, nothing was ever baked - leave the
	// followers (possibly live-recorded) alone.
	if (!coll->params.getParamFromCC(paramIDForMacro(macroIndex))) {
		return;
	}

	// Create any follower params we'll write BEFORE taking pointers into the vector: an insert
	// memmoves/reallocs the element storage, which would dangle the leader pointer taken below.
	for (int32_t slot = 0; slot < kNumFollowerSlots; slot++) {
		MacroFollowerSlot& follower = instrument->macros[macroIndex].followers[slot];
		if (follower.cc != kFollowerCCNone && follower.send
		    && !isFollowerShadowed(instrument->macros, macroIndex, slot)) {
			coll->params.getOrCreateParamFromCC(follower.cc, 0);
		}
	}
	MIDIParam* leaderParam = coll->params.getParamFromCC(paramIDForMacro(macroIndex));
	if (!leaderParam) {
		return;
	}
	AutoParam* leader = &leaderParam->param;

	ModelStackWithThreeMainThings* three =
	    modelStack->addNoteRow(0, nullptr)->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);

	for (int32_t slot = 0; slot < kNumFollowerSlots; slot++) {
		MacroFollowerSlot& follower = instrument->macros[macroIndex].followers[slot];
		if (follower.cc == kFollowerCCNone) {
			continue;
		}
		// A shadowed follower's CC lane belongs to its owner - don't clear or bake it.
		if (isFollowerShadowed(instrument->macros, macroIndex, slot)) {
			continue;
		}
		bakeFollower(coll, summary, three, follower, leader, action);
		refreshAutomationGridIfShowingCC(clip, follower.cc);
	}
	instrument->editedByUser = true;
}

void reFanMacro(Clip* clip, int32_t macroIndex, Action* action) {
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return;
	}
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
	fanOutMacroLane(instrument, clip, macroIndex, modelStackWithTimelineCounter, action);
}

void reFanFollower(Clip* clip, int32_t macroIndex, int32_t slot, Action* action) {
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return;
	}
	MacroFollowerSlot& follower = instrument->macros[macroIndex].followers[slot];
	if (follower.cc == kFollowerCCNone) {
		return;
	}
	// A shadowed follower's CC lane belongs to its owner - don't clear or bake it.
	if (isFollowerShadowed(instrument->macros, macroIndex, slot)) {
		return;
	}
	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll || !coll->params.getParamFromCC(paramIDForMacro(macroIndex))) {
		return; // macro lane never touched on this clip - nothing baked, nothing to redo
	}
	if (follower.send) {
		coll->params.getOrCreateParamFromCC(follower.cc, 0); // create before taking the leader pointer
	}
	MIDIParam* leaderParam = coll->params.getParamFromCC(paramIDForMacro(macroIndex));
	if (!leaderParam) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithThreeMainThings* three =
	    modelStack->addTimelineCounter(clip)
	        ->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);
	bakeFollower(coll, summary, three, follower, &leaderParam->param, action);
	refreshAutomationGridIfShowingCC(clip, follower.cc);
	instrument->editedByUser = true;
}

void changeFollowerCC(Clip* clip, int32_t macroIndex, int32_t slot, uint8_t newCC) {
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return;
	}
	MacroFollowerSlot& follower = instrument->macros[macroIndex].followers[slot];
	uint8_t oldCC = follower.cc;
	if (newCC == oldCC) {
		return;
	}
	follower.cc = newCC;
	instrument->editedByUser = true;

	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll || !coll->params.getParamFromCC(paramIDForMacro(macroIndex))) {
		return; // no bake ever happened on this clip: pure config change
	}

	// One undoable action for the pair of lane writes; consecutive detents of a scroll gesture join it.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::ALLOWED);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithThreeMainThings* three =
	    modelStack->addTimelineCounter(clip)
	        ->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);

	// Deal with the CC we left: if another follower still targets it, hand ownership over (it may have
	// been shadowed by us - re-bake it); otherwise clear our bake so it doesn't play as a ghost lane.
	if (oldCC != kFollowerCCNone) {
		int32_t ownerSlot = 0;
		int32_t ownerMacro = findFollowerCCOwner(instrument->macros, oldCC, macroIndex, slot, &ownerSlot);
		MIDIParam* oldParam = coll->params.getParamFromCC(oldCC);
		if (oldParam) {
			ModelStackWithAutoParam* modelStackWithParam =
			    three->addParamCollectionAndId(coll, summary, oldCC)->addAutoParam(&oldParam->param);
			oldParam->param.deleteAutomation(action, modelStackWithParam, false);
			refreshAutomationGridIfShowingCC(clip, oldCC);
		}
		if (ownerMacro >= 0) {
			reFanFollower(clip, ownerMacro, ownerSlot, action); // promote the new owner's bake
		}
	}

	// Bake the macro curve into the new CC - unless an earlier follower owns it (we're shadowed: keep
	// the config but leave the owner's lane alone; the UI shows the conflict and keeps our LED off).
	if (newCC != kFollowerCCNone && findShadowingOwner(instrument->macros, newCC, macroIndex, slot) < 0) {
		if (follower.send) {
			coll->params.getOrCreateParamFromCC(newCC, 0); // create before taking the leader pointer
		}
		MIDIParam* leaderParam = coll->params.getParamFromCC(paramIDForMacro(macroIndex));
		if (leaderParam) {
			bakeFollower(coll, summary, three, follower, &leaderParam->param, action);
			refreshAutomationGridIfShowingCC(clip, newCC);
		}
	}

	view.setModLedStates(); // the mod-button LEDs show follower assignment while a macro lane is shown
}

} // namespace MIDIMacro
