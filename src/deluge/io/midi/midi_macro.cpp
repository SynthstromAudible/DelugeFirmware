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
#include "modulation/params/param_set.h"
#include "storage/storage_manager.h"
#include "util/d_stringbuf.h"
#include "util/misc.h"
#include <algorithm>
#include <string.h>

#define MIDI_MACROS_TAG "midiMacros"      // per-instrument block, embedded in the instrument's XML
#define MIDI_MACRO_PRESET_TAG "midiMacro" // preset file root (targets only)
#define MIDI_MACRO_ELEMENT_TAG "macro"    // one macro (source + targets)
#define MIDI_MACRO_SOURCE_TAG "source"    // the learned source CC
#define MIDI_MACRO_TARGET_PREFIX "target"

namespace MIDIMacro {

int32_t presetMacroIndex = 0;

bool isEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::MidiMacro) == RuntimeFeatureStateToggle::On;
}

int32_t findTargetCCOwner(const Macro* macros, uint8_t cc, int32_t exceptMacro, int32_t exceptSlot, int32_t* slotOut) {
	if (cc == kTargetCCNone) {
		return -1;
	}
	for (int32_t m = 0; m < kNumMacros; m++) {
		for (int32_t f = 0; f < kNumTargetSlots; f++) {
			if (m == exceptMacro && f == exceptSlot) {
				continue;
			}
			if (macros[m].targets[f].cc == cc) {
				if (slotOut != nullptr) {
					*slotOut = f;
				}
				return m;
			}
		}
	}
	return -1;
}

// Ownership rank of a target position: targets of ACTIVE macros outrank inactive ones (an inactive
// macro must never block an active one from driving a CC), then scan order (macro, then slot)
// breaks ties. Lower rank wins. Packed so the active bit dominates: macro fits in 2 bits (4
// macros), slot in 3 (8 slots).
static inline int32_t targetRank(const Macro* macros, int32_t m, int32_t f) {
	return ((macros[m].active ? 0 : 1) << 8) | (m << 3) | f;
}

// The target that owns (drives) `cc` under the rank order above, or -1 if nothing targets it; if
// non-null, *slotOut receives the owning slot.
static int32_t findCCOwner(const Macro* macros, uint8_t cc, int32_t* slotOut) {
	if (cc == kTargetCCNone) {
		return -1;
	}
	int32_t owner = -1;
	int32_t bestRank = 0;
	for (int32_t m = 0; m < kNumMacros; m++) {
		for (int32_t f = 0; f < kNumTargetSlots; f++) {
			if (macros[m].targets[f].cc != cc) {
				continue;
			}
			int32_t rank = targetRank(macros, m, f);
			if (owner < 0 || rank < bestRank) {
				owner = m;
				bestRank = rank;
				if (slotOut != nullptr) {
					*slotOut = f;
				}
			}
		}
	}
	return owner;
}

int32_t findShadowingOwner(const Macro* macros, uint8_t cc, int32_t macroIndex, int32_t slot) {
	if (cc == kTargetCCNone) {
		return -1;
	}
	// (macroIndex, slot) itself is skipped rather than matched: `cc` may be a pending value the
	// slot doesn't hold yet, and this answers "would this position holding cc be the owner?".
	int32_t ourRank = targetRank(macros, macroIndex, slot);
	int32_t owner = -1;
	int32_t bestRank = ourRank;
	for (int32_t m = 0; m < kNumMacros; m++) {
		for (int32_t f = 0; f < kNumTargetSlots; f++) {
			if (m == macroIndex && f == slot) {
				continue;
			}
			if (macros[m].targets[f].cc != cc) {
				continue;
			}
			int32_t rank = targetRank(macros, m, f);
			if (rank < bestRank) {
				owner = m;
				bestRank = rank;
			}
		}
	}
	return owner;
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
		buf.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_TARGET_CC)); // "CC"
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

bool targetHasConflict(const Macro* macros, int32_t macroIndex, int32_t slot) {
	// Active-aware ranking makes shadowing cover both conflict flavors: a duplicate outranked among
	// active macros, and - for an inactive macro - any active macro holding the CC.
	return isTargetShadowed(macros, macroIndex, slot);
}

bool anyMacroConfigured(Macro* macros) {
	for (int32_t m = 0; m < kNumMacros; m++) {
		if (!macros[m].active || macros[m].source.containsSomething() || macros[m].sourceKnob >= 0) {
			return true;
		}
		for (const MacroTargetSlot& target : macros[m].targets) {
			if (target.cc != kTargetCCNone) {
				return true;
			}
		}
	}
	return false;
}

// Scale a source value 0..127 linearly onto target [from, to] (from>to inverts), rounding to nearest.
static inline int32_t scaleTarget(const MacroTargetSlot& target, int32_t value) {
	int32_t span = (int32_t)target.to - (int32_t)target.from;
	int32_t out = target.from + (span * value + (span >= 0 ? 63 : -63)) / 127;
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

static void sendToTargets(MIDIInstrument* instrument, Clip* clip, ModelStackWithTimelineCounter* modelStack,
                          Macro& macro, int32_t value) {
	RootUI* rootUI = getRootUI();
	int32_t macroIndex = (int32_t)(&macro - instrument->macros);
	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		MacroTargetSlot& target = macro.targets[slot];
		if (target.cc == kTargetCCNone || !target.send) {
			continue;
		}
		// A shadowed target (another target owns its CC) is inert - only the owner drives.
		if (isTargetShadowed(instrument->macros, macroIndex, slot)) {
			continue;
		}
		int32_t valueBig = (scaleTarget(target, value) - 64) << 25;
		// writes the value into the clip's own CC param, so it reaches the output and gets
		// recorded into that CC's automation lane like a manual change to it would
		instrument->processParamFromInputMIDIChannel(target.cc, valueBig, modelStack);
		// the param write doesn't notify the UI, so refresh the automation editor grid ourselves
		// if it's showing this CC. Can't use possiblyRefreshAutomationEditorGrid() here: for MIDI
		// clips, automation view tracks the selected lane by CC number alone and leaves
		// lastSelectedParamKind unset, so its param-kind comparison never matches
		if (rootUI == &automationView && !automationView.onArrangerView && clip->lastSelectedParamID == target.cc) {
			uiNeedsRendering(&automationView);
		}
	}
}

bool tryMacro(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value) {
	if (!isEnabled()) {
		return false;
	}
	// Don't drive targets while a song is loading. On boot a controller's power-on CC burst (e.g. the
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
		if (macro.source.equalsNoteOrCC(&cable, channelOrZone + IS_A_CC, ccNumber)) {
			sendToTargets(instrument, clip, modelStackWithTimelineCounter, macro, value);
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
	// Cheap pre-check so a normal (non-source) knob turn bails before building a modelStack.
	bool anyMatch = false;
	for (Macro& macro : instrument->macros) {
		if (macro.active && macro.sourceKnob == whichKnob) {
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
		if (!macro.active || macro.sourceKnob != whichKnob) {
			continue;
		}
		// A gold-knob source is a relative encoder: accumulate its position, then drive the targets.
		int32_t pos = std::clamp<int32_t>((int32_t)macro.sourceKnobPos + offset, 0, kMaxValue);
		macro.sourceKnobPos = pos;
		sendToTargets(instrument, clip, modelStackWithTimelineCounter, macro, pos);
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
	// An active macro with no targets must not consume the knob either: macros are active by
	// default, so without this a track that never opted in would lose both gold knobs on this row.
	bool anyTarget = false;
	for (const MacroTargetSlot& target : macro.targets) {
		if (target.cc != kTargetCCNone) {
			anyTarget = true;
			break;
		}
	}
	if (!anyTarget) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	int32_t pos = std::clamp<int32_t>((int32_t)macro.sourceKnobPos + offset, 0, kMaxValue);
	macro.sourceKnobPos = pos;
	sendToTargets(instrument, clip, modelStackWithTimelineCounter, macro, pos);
	return true;
}

// target slot tag names are "target1" .. "target8"; returns the slot index, or -1 if not one.
static int32_t targetSlotFromTagName(char const* tagName) {
	constexpr int32_t prefixLen = 6; // strlen("target")
	if (!memcmp(tagName, MIDI_MACRO_TARGET_PREFIX, prefixLen) && tagName[prefixLen] >= '1'
	    && tagName[prefixLen] < '1' + kNumTargetSlots && !tagName[prefixLen + 1]) {
		return tagName[prefixLen] - '1';
	}
	return -1;
}

// Writes the configured <target1..8/> elements of a macro. Used by both the instrument block and presets.
static void writeTargetsToFile(Serializer& writer, Macro& macro) {
	for (int32_t i = 0; i < kNumTargetSlots; i++) {
		MacroTargetSlot& target = macro.targets[i];
		if (target.cc != kTargetCCNone) {
			char tagName[10] = MIDI_MACRO_TARGET_PREFIX "1"; // "target1"
			tagName[6] = '1' + i;
			writer.writeOpeningTagBeginning(tagName);
			writer.writeAttribute("cc", target.cc, false);
			writer.writeAttribute("from", target.from, false);
			writer.writeAttribute("to", target.to, false);
			writer.writeAttribute("send", target.send ? 1 : 0, false);
			writer.closeTag();
		}
	}
}

// Writes one <macro> element (self-closing if empty), as it appears in the instrument block.
static void writeMacroToFile(Serializer& writer, Macro& macro) {
	// Does this macro have any children to write? (A learned source or at least one configured target.)
	bool hasChildren = macro.source.containsSomething();
	for (int32_t i = 0; i < kNumTargetSlots && !hasChildren; i++) {
		hasChildren = macro.targets[i].cc != kTargetCCNone;
	}

	writer.writeOpeningTagBeginning(MIDI_MACRO_ELEMENT_TAG);
	writer.writeAttribute("active", macro.active ? 1 : 0, false);
	if (macro.sourceKnob >= 0) {
		writer.writeAttribute("sourceKnob", macro.sourceKnob, false); // gold-knob source rides on the tag
	}

	// Self-close empty macros (<macro active="1"/>). The XML reader mis-parses an empty but
	// explicitly-closed element that carries an attribute (<macro active="1"></macro>), which
	// desyncs the rest of the file; a self-closing tag avoids that. Macros must stay positional
	// (slot 0..3 = macro index), so we always write all of them rather than skipping empties.
	if (!hasChildren) {
		writer.closeTag();
		return;
	}

	writer.writeOpeningTagEnd();

	macro.source.writeCCToFile(writer, MIDI_MACRO_SOURCE_TAG);

	writeTargetsToFile(writer, macro);

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

// Reads a target's cc/from/to/send attributes (e.g. <target1 cc="42" from="0" to="100" send="1" />).
static void readTargetFromFile(Deserializer& reader, MacroTargetSlot& target) {
	int32_t cc = kTargetCCNone;
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
	target.cc = (cc >= 0 && cc <= kMaxTargetCC) ? cc : kTargetCCNone;
	target.from = std::clamp<int32_t>(from, 0, kMaxValue);
	target.to = std::clamp<int32_t>(to, 0, kMaxValue);
	target.send = send;
}

static void readMacroFromFile(Deserializer& reader, Macro& macro) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// accept legacy "enabled" as well as "active"
		if (!strcmp(tagName, "active") || !strcmp(tagName, "enabled")) {
			macro.active = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, "sourceKnob")) {
			int32_t k = reader.readTagOrAttributeValueInt();
			macro.sourceKnob = (k >= 0 && k < kNumPhysicalModKnobs) ? (int8_t)k : -1;
		}
		else if (!strcmp(tagName, MIDI_MACRO_SOURCE_TAG)) {
			macro.source.readCCFromFile(reader);
		}
		else {
			int32_t slot = targetSlotFromTagName(tagName);
			if (slot >= 0) {
				readTargetFromFile(reader, macro.targets[slot]);
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

// Writes a preset: targets only (<midiMacro><target1..8/></midiMacro>). A preset is a portable
// target configuration - it deliberately omits the source and the macro's active state so it can
// be applied to any macro. The caller (save browser) owns createXMLFile()/closeFileAfterWriting().
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex) {
	writer.writeOpeningTagBeginning(MIDI_MACRO_PRESET_TAG);
	writer.writeOpeningTagEnd();
	writeTargetsToFile(writer, macros[macroIndex]);
	writer.writeClosingTag(MIDI_MACRO_PRESET_TAG);
}

// Loads a preset's targets into macros[macroIndex], replacing that macro's targets only and
// leaving its source and active state untouched. Loaded CCs that another macro also targets
// resolve by ownership rank; the fan-out below only bakes the slots this macro owns.
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex) {
	Error error = StorageManager::openXMLFile(fp, smDeserializer, MIDI_MACRO_PRESET_TAG);
	if (error != Error::NONE) {
		return error;
	}

	// Remember which CCs the old target set targeted, so their baked lanes can be cleared if the
	// preset no longer uses them (otherwise they'd keep playing as ghost lanes).
	uint8_t oldCCs[kNumTargetSlots];
	for (int32_t f = 0; f < kNumTargetSlots; f++) {
		oldCCs[f] = macros[macroIndex].targets[f].cc;
	}

	// Reset only the targets so slots the preset doesn't configure are cleared, not left over.
	for (MacroTargetSlot& target : macros[macroIndex].targets) {
		target = MacroTargetSlot{};
	}

	Deserializer& reader = *activeDeserializer;
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		int32_t slot = targetSlotFromTagName(tagName);
		if (slot >= 0) {
			readTargetFromFile(reader, macros[macroIndex].targets[slot]);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();

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
		for (int32_t f = 0; f < kNumTargetSlots; f++) {
			uint8_t oldCC = oldCCs[f];
			if (oldCC == kTargetCCNone || findTargetCCOwner(macros, oldCC, -1, -1) >= 0) {
				continue; // dropped CC still targeted by some target - its bake stays
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
	reFanMacro(clip, macroIndex, nullptr); // new target set -> re-bake the macro lane into it
	view.setModLedStates();                // refresh the target-assignment LEDs

	return Error::NONE;
}

// The expression pseudo-CCs (bend/aftertouch/Y) live in the clip's ExpressionParamSet, not its MIDI
// param collection; returns the dimension index, or -1 for a real CC. Same mapping as
// MIDIInstrument::getParamToControlFromInputMIDIChannel - the path sendToTargets() writes through.
static int32_t expressionDimensionFromCC(uint8_t cc) {
	switch (cc) {
	case CC_NUMBER_PITCH_BEND:
		return 0;
	case CC_NUMBER_Y_AXIS:
		return 1;
	case CC_NUMBER_AFTERTOUCH:
		return 2;
	default:
		return -1;
	}
}

bool capture(Clip* clip, int32_t macroIndex, bool toMax) {
	if (getCurrentUI() == &loadSongUI) {
		return false;
	}
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return false;
	}
	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll) {
		return false;
	}

	bool changed = false;
	for (MacroTargetSlot& target : instrument->macros[macroIndex].targets) {
		if (target.cc == kTargetCCNone) {
			continue;
		}
		// Read this CC's current value on THIS clip - the inverse of the write in sendToTargets(). A
		// CC that's never been moved/recorded here has no param (and no known value): leave its
		// endpoint alone rather than capturing a made-up default.
		AutoParam* param = nullptr;
		MIDIParam* midiParam = coll->params.getParamFromCC(target.cc);
		if (midiParam) {
			param = &midiParam->param;
		}
		else {
			int32_t dimension = expressionDimensionFromCC(target.cc);
			ExpressionParamSet* expression = (dimension >= 0) ? clip->paramManager.getExpressionParamSet() : nullptr;
			if (expression) {
				param = &expression->params[dimension];
			}
		}
		if (!param) {
			continue;
		}
		(toMax ? target.to : target.from) = (uint8_t)bigValueToCC(param->getCurrentValue());
		changed = true;
	}
	if (changed) {
		instrument->editedByUser = true;
	}
	return changed;
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

// Clears one target's lane and, when sending, mirrors the source lane into it scaled by [from, to].
// The caller must not create any params between resolving `source` and this call: an insert moves the
// vector's storage and would dangle the pointer.
static void bakeTarget(MIDIParamCollection* coll, ParamCollectionSummary* summary, ModelStackWithThreeMainThings* three,
                       const MacroTargetSlot& target, AutoParam* source, Action* action) {
	MIDIParam* targetParam = coll->params.getParamFromCC(target.cc);
	if (!targetParam) {
		return; // never existed: nothing to clear, and nothing will be written (send off)
	}
	ModelStackWithAutoParam* modelStackWithParam =
	    three->addParamCollectionAndId(coll, summary, target.cc)->addAutoParam(&targetParam->param);

	// Overwrite: clear first (deleteAutomation snapshots into `action` for undo when given).
	targetParam->param.deleteAutomation(action, modelStackWithParam, false);

	if (target.send) {
		for (int32_t n = 0; n < source->nodes.getNumElements(); n++) {
			ParamNode* node = source->nodes.getElement(n);
			int32_t out = scaleTarget(target, bigValueToCC(node->value));
			targetParam->param.setNodeAtPos(node->pos, (out - 64) << 25, node->interpolated);
		}
		// Base value for regions before the first node / when the source has no nodes.
		int32_t base = scaleTarget(target, bigValueToCC(source->getCurrentValue()));
		targetParam->param.setCurrentValueBasicForSetup((base - 64) << 25);
	}
}

static void refreshAutomationGridIfShowingCC(Clip* clip, uint8_t cc) {
	if (getRootUI() == &automationView && !automationView.onArrangerView && clip->lastSelectedParamID == cc) {
		uiNeedsRendering(&automationView);
	}
}

// Mirror macroIndex's source-lane automation (pseudo-CC paramIDForMacro) into each target CC's
// automation on `clip`, scaled. Overwrites the target lanes; snapshots them into `action` first.
static void fanOutMacroLane(MIDIInstrument* instrument, Clip* clip, int32_t macroIndex,
                            ModelStackWithTimelineCounter* modelStack, Action* action) {
	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll) {
		return;
	}
	// If the macro lane's param was never created on this clip, nothing was ever baked - leave the
	// targets (possibly live-recorded) alone.
	if (!coll->params.getParamFromCC(paramIDForMacro(macroIndex))) {
		return;
	}

	// Create any target params we'll write BEFORE taking pointers into the vector: an insert
	// memmoves/reallocs the element storage, which would dangle the source pointer taken below.
	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		MacroTargetSlot& target = instrument->macros[macroIndex].targets[slot];
		if (target.cc != kTargetCCNone && target.send && !isTargetShadowed(instrument->macros, macroIndex, slot)) {
			coll->params.getOrCreateParamFromCC(target.cc, 0);
		}
	}
	MIDIParam* sourceParam = coll->params.getParamFromCC(paramIDForMacro(macroIndex));
	if (!sourceParam) {
		return;
	}
	AutoParam* source = &sourceParam->param;

	ModelStackWithThreeMainThings* three =
	    modelStack->addNoteRow(0, nullptr)->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);

	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		MacroTargetSlot& target = instrument->macros[macroIndex].targets[slot];
		if (target.cc == kTargetCCNone) {
			continue;
		}
		// A shadowed target's CC lane belongs to its owner - don't clear or bake it.
		if (isTargetShadowed(instrument->macros, macroIndex, slot)) {
			continue;
		}
		bakeTarget(coll, summary, three, target, source, action);
		refreshAutomationGridIfShowingCC(clip, target.cc);
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

void reFanTarget(Clip* clip, int32_t macroIndex, int32_t slot, Action* action) {
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return;
	}
	MacroTargetSlot& target = instrument->macros[macroIndex].targets[slot];
	if (target.cc == kTargetCCNone) {
		return;
	}
	// A shadowed target's CC lane belongs to its owner - don't clear or bake it.
	if (isTargetShadowed(instrument->macros, macroIndex, slot)) {
		return;
	}
	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll || !coll->params.getParamFromCC(paramIDForMacro(macroIndex))) {
		return; // macro lane never touched on this clip - nothing baked, nothing to redo
	}
	if (target.send) {
		coll->params.getOrCreateParamFromCC(target.cc, 0); // create before taking the source pointer
	}
	MIDIParam* sourceParam = coll->params.getParamFromCC(paramIDForMacro(macroIndex));
	if (!sourceParam) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithThreeMainThings* three =
	    modelStack->addTimelineCounter(clip)
	        ->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);
	bakeTarget(coll, summary, three, target, &sourceParam->param, action);
	refreshAutomationGridIfShowingCC(clip, target.cc);
	instrument->editedByUser = true;
}

void changeTargetCC(Clip* clip, int32_t macroIndex, int32_t slot, uint8_t newCC) {
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return;
	}
	MacroTargetSlot& target = instrument->macros[macroIndex].targets[slot];
	uint8_t oldCC = target.cc;
	if (newCC == oldCC) {
		return;
	}
	target.cc = newCC;
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

	// Deal with the CC we left: if another target still targets it, hand ownership to whichever one
	// now ranks first (it may have been shadowed by us - re-bake it); otherwise clear our bake so it
	// doesn't play as a ghost lane. target.cc is already reassigned, so findCCOwner excludes us.
	if (oldCC != kTargetCCNone) {
		int32_t ownerSlot = 0;
		int32_t ownerMacro = findCCOwner(instrument->macros, oldCC, &ownerSlot);
		MIDIParam* oldParam = coll->params.getParamFromCC(oldCC);
		if (oldParam) {
			ModelStackWithAutoParam* modelStackWithParam =
			    three->addParamCollectionAndId(coll, summary, oldCC)->addAutoParam(&oldParam->param);
			oldParam->param.deleteAutomation(action, modelStackWithParam, false);
			refreshAutomationGridIfShowingCC(clip, oldCC);
		}
		if (ownerMacro >= 0) {
			reFanTarget(clip, ownerMacro, ownerSlot, action); // promote the new owner's bake
		}
	}

	// Bake the macro curve into the new CC - unless another target outranks us there (we're shadowed:
	// keep the config but leave the owner's lane alone; the UI shows the conflict and keeps our LED off).
	if (newCC != kTargetCCNone && findShadowingOwner(instrument->macros, newCC, macroIndex, slot) < 0) {
		if (target.send) {
			coll->params.getOrCreateParamFromCC(newCC, 0); // create before taking the source pointer
		}
		MIDIParam* sourceParam = coll->params.getParamFromCC(paramIDForMacro(macroIndex));
		if (sourceParam) {
			bakeTarget(coll, summary, three, target, &sourceParam->param, action);
			refreshAutomationGridIfShowingCC(clip, newCC);
		}
	}

	view.setModLedStates(); // the mod-button LEDs show target assignment while a macro lane is shown
}

void setMacroActive(Clip* clip, int32_t macroIndex, bool active) {
	MIDIInstrument* instrument = midiClipInstrument(clip);
	if (!instrument) {
		return;
	}
	Macro* macros = instrument->macros;
	if (macros[macroIndex].active == active) {
		return;
	}

	// Ownership ranks active macros above inactive ones, so flipping `active` can hand this macro's
	// duplicated CCs to/from another targeter. Record the pre-flip owners to detect the transfers.
	int32_t oldOwnerMacros[kNumTargetSlots];
	int32_t oldOwnerSlots[kNumTargetSlots];
	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		oldOwnerSlots[slot] = 0;
		oldOwnerMacros[slot] = findCCOwner(macros, macros[macroIndex].targets[slot].cc, &oldOwnerSlots[slot]);
	}

	macros[macroIndex].active = active;
	instrument->editedByUser = true;

	ParamCollectionSummary* summary;
	MIDIParamCollection* coll = getMIDIParams(clip, &summary);
	if (!coll) {
		return;
	}

	Action* action = nullptr;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithThreeMainThings* three =
	    modelStack->addTimelineCounter(clip)
	        ->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(instrument->toModControllable(), &clip->paramManager);

	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		uint8_t cc = macros[macroIndex].targets[slot].cc;
		if (cc == kTargetCCNone) {
			continue;
		}
		// duplicates of the CC within this macro share one lane - handle each CC once
		bool alreadyHandled = false;
		for (int32_t prev = 0; prev < slot; prev++) {
			if (macros[macroIndex].targets[prev].cc == cc) {
				alreadyHandled = true;
				break;
			}
		}
		if (alreadyHandled) {
			continue;
		}
		int32_t newOwnerSlot = 0;
		int32_t newOwnerMacro = findCCOwner(macros, cc, &newOwnerSlot);
		if (newOwnerMacro == oldOwnerMacros[slot] && newOwnerSlot == oldOwnerSlots[slot]) {
			continue; // same owner as before the flip - its bake already stands
		}
		// The CC changed hands. Clear the old owner's bake first (only if that macro ever baked -
		// same ghost-lane rule as changeTargetCC), then promote the new owner's; if the new owner's
		// lane was never automated, the CC simply stays clear.
		if (oldOwnerMacros[slot] >= 0 && coll->params.getParamFromCC(paramIDForMacro(oldOwnerMacros[slot]))) {
			MIDIParam* param = coll->params.getParamFromCC(cc);
			if (param) {
				if (!action) {
					action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
				}
				ModelStackWithAutoParam* modelStackWithParam =
				    three->addParamCollectionAndId(coll, summary, cc)->addAutoParam(&param->param);
				param->param.deleteAutomation(action, modelStackWithParam, false);
				refreshAutomationGridIfShowingCC(clip, cc);
			}
		}
		if (!action) {
			action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
		}
		reFanTarget(clip, newOwnerMacro, newOwnerSlot, action);
	}
}

} // namespace MIDIMacro
