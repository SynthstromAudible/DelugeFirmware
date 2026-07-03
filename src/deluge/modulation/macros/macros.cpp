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

#include "modulation/macros/macros.h"
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
#include "model/instrument/melodic_instrument.h"
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

namespace params = deluge::modulation::params;

#define MACROS_TAG "macros"            // per-instrument block, embedded in the instrument's XML
#define MACRO_PRESET_TAG "macroPreset" // preset file root (targets only)
#define MACRO_ELEMENT_TAG "macro"      // one macro (source + targets)
#define MACRO_SOURCE_TAG "source"      // the learned source CC
#define MACRO_TARGET_PREFIX "target"

namespace Macros {

int32_t presetMacroIndex = 0;

bool isEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::MacroSystem) == RuntimeFeatureStateToggle::On;
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

// Appends a destination's label: "Macro N" for a cascade destination (always the plain macro
// number, never its user name), the param's display name on a synth track, else the MIDI device's
// CC name if loaded, else "CC <n>".
void appendDestName(StringBuf& buf, MelodicInstrument* instrument, uint8_t cc) {
	if (isMacroParamID(cc)) {
		buf.append(deluge::l10n::get(static_cast<deluge::l10n::String>(
		    util::to_underlying(deluge::l10n::String::STRING_FOR_MACRO_1) + macroIndexFromParamID(cc))));
		return;
	}
	if (instrument && instrument->type == OutputType::SYNTH) {
		params::Kind kind;
		int32_t id;
		decodeSynthDestination(cc, &kind, &id);
		buf.append(params::getParamDisplayName(kind, id));
		return;
	}
	std::string_view name{};
	if (instrument && instrument->type == OutputType::MIDI_OUT) {
		name = ((MIDIInstrument*)instrument)->getNameFromCC(cc);
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

// Appends "<dest>\nused by\nMacro N" (OLED) / "<dest> used by Macro N" (7SEG). The destination is
// resolved against the selected/active clip's instrument, same as the conflict machinery.
static void appendCCUsedBy(StringBuf& buf, uint8_t cc, int32_t ownerMacro) {
	appendDestName(buf, macroClipInstrument(midiFollow.getSelectedOrActiveClip()), cc);
	buf.append(display->haveOLED() ? '\n' : ' ');
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
		if (!macros[m].active || macros[m].source.containsSomething() || macros[m].sourceKnob >= 0
		    || !macros[m].name.isEmpty()) {
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

// Scale a source value 0..127 linearly onto target [from, to] (from>to inverts), rounding to
// nearest. outMax is the domain's target ceiling (127 CC / 128 knobPos).
static inline int32_t scaleTarget(const MacroTargetSlot& target, int32_t value, int32_t outMax) {
	int32_t span = (int32_t)target.to - (int32_t)target.from;
	int32_t out = target.from + (span * value + (span >= 0 ? 63 : -63)) / 127;
	return std::clamp<int32_t>(out, 0, outMax);
}

// Saturating conversion from a big param value to a 0..127 CC value. Reuses the collection's guarded
// rounding: a maxed automation node holds INT32_MAX (knobPosToParamValue special case), which the
// naive (v + (1<<24)) >> 25 would wrap negative and decode as 0 instead of 127.
static inline int32_t bigValueToCC(int32_t value) {
	return std::clamp<int32_t>(MIDIParamCollection::autoparamValueToCC(value) + 64, 0, (int32_t)kMaxValue);
}

Domain domainForOutput(Output* output) {
	return (output->type == OutputType::SYNTH) ? Domain::SYNTH : Domain::MIDI;
}

MelodicInstrument* macroClipInstrument(Clip* clip) {
	if (!clip || !clip->output
	    || (clip->output->type != OutputType::MIDI_OUT && clip->output->type != OutputType::SYNTH)) {
		return nullptr;
	}
	return (MelodicInstrument*)clip->output;
}

void decodeSynthDestination(uint8_t dest, params::Kind* kindOut, int32_t* idOut) {
	// A cascade id addresses the downstream macro's own lane param on this domain.
	if (isMacroParamID(dest)) {
		*kindOut = params::Kind::UNPATCHED_SOUND;
		*idOut = params::UNPATCHED_MACRO_1 + macroIndexFromParamID(dest);
		return;
	}
	if (dest >= params::UNPATCHED_START) {
		*kindOut = params::Kind::UNPATCHED_SOUND;
		*idOut = dest - params::UNPATCHED_START;
		return;
	}
	*kindOut = params::Kind::PATCHED;
	*idOut = dest;
}

int32_t synthDestinationForParam(params::Kind kind, int32_t paramID) {
	if (kind == params::Kind::PATCHED && paramID >= 0 && paramID < params::UNPATCHED_START) {
		return paramID;
	}
	if (kind == params::Kind::UNPATCHED_SOUND && paramID >= 0
	    && paramID < params::UNPATCHED_SOUND_MAX_NUM
	    // the lane params are only addressable as cascade ids, never as ordinary destinations
	    && (paramID < params::UNPATCHED_MACRO_1 || paramID > params::UNPATCHED_MACRO_4)) {
		return params::UNPATCHED_START + paramID;
	}
	return -1;
}

uint8_t laneDestination(Domain domain, int32_t macroIndex) {
	if (domain == Domain::SYNTH) {
		return params::UNPATCHED_START + params::UNPATCHED_MACRO_1 + macroIndex;
	}
	return paramIDForMacro(macroIndex);
}

int32_t macroIndexForLaneSelection(Output* output, params::Kind kind, int32_t paramID) {
	if (!output) {
		return -1;
	}
	if (output->type == OutputType::MIDI_OUT) {
		return isMacroParamID(paramID) ? macroIndexFromParamID(paramID) : -1;
	}
	if (output->type == OutputType::SYNTH && kind == params::Kind::UNPATCHED_SOUND
	    && paramID >= params::UNPATCHED_MACRO_1 && paramID <= params::UNPATCHED_MACRO_4) {
		return paramID - params::UNPATCHED_MACRO_1;
	}
	return -1;
}

// The ordered SYNTH destination list: every targetable byte, in automation-view scroll order (the
// single source of param ordering). Built once, lazily, into a fixed buffer (no heap); excludes
// EXPRESSION entries, patch cables (not encodable as a byte) and the macro lane params
// (cascade-only).
static const uint8_t* synthDestinationList(int32_t* countOut) {
	static uint8_t list[kNumNonGlobalParamsForAutomation];
	static int32_t count = -1;
	if (count < 0) {
		count = 0;
		for (const auto& [kind, id] : nonGlobalParamsForAutomation) {
			int32_t dest = synthDestinationForParam(kind, id);
			if (dest >= 0) {
				list[count++] = (uint8_t)dest;
			}
		}
	}
	*countOut = count;
	return list;
}

// The number of non-cascade destinations in a domain's dial space.
static int32_t numBaseDestinations(Domain domain) {
	if (domain == Domain::SYNTH) {
		int32_t count = 0;
		synthDestinationList(&count);
		return count;
	}
	return kMaxTargetCC + 1;
}

int32_t numDestinations(Domain domain, int32_t macroIndex) {
	return numBaseDestinations(domain) + numCascadeDestinations(macroIndex);
}

int32_t destinationForPosition(Domain domain, int32_t macroIndex, int32_t position) {
	if (position < 0 || position >= numDestinations(domain, macroIndex)) {
		return -1;
	}
	int32_t numBase = numBaseDestinations(domain);
	if (position >= numBase) {
		return paramIDForMacro(macroIndex) + 1 + (position - numBase);
	}
	if (domain == Domain::SYNTH) {
		int32_t count = 0;
		return synthDestinationList(&count)[position];
	}
	return position;
}

int32_t positionForDestination(Domain domain, int32_t macroIndex, uint8_t dest) {
	int32_t numBase = numBaseDestinations(domain);
	if (isMacroParamID(dest)) {
		int32_t offset = macroIndexFromParamID(dest) - macroIndex - 1;
		return (offset >= 0) ? numBase + offset : -1;
	}
	if (domain == Domain::SYNTH) {
		int32_t count = 0;
		const uint8_t* list = synthDestinationList(&count);
		for (int32_t i = 0; i < count; i++) {
			if (list[i] == dest) {
				return i;
			}
		}
		return -1;
	}
	return (dest <= kMaxTargetCC) ? dest : -1;
}

bool isValidTargetDestination(Domain domain, int32_t cc, int32_t macroIndex) {
	if (isCascadeDestination(cc, macroIndex)) {
		return true;
	}
	if (domain == Domain::SYNTH) {
		return cc >= 0 && cc <= UINT8_MAX && positionForDestination(domain, macroIndex, (uint8_t)cc) >= 0;
	}
	return cc >= 0 && cc <= kMaxTargetCC;
}

// ── Per-domain value conversion ──
// Macro endpoints and outputs live in "units": CC values 0..127 on MIDI, knob positions 0..128 on
// synths. MIDI converts with the fixed (v-64)<<25 mapping; synths route through the destination
// collection's own conversions so bipolar params (pan) and per-param overrides scale correctly.
static inline int32_t unitsToParamValue(ModelStackWithAutoParam* msp, Domain domain, int32_t units) {
	if (domain == Domain::SYNTH) {
		return msp->paramCollection->knobPosToParamValue(units - 64, msp);
	}
	return (units - 64) << 25;
}
static inline int32_t paramValueToUnits(ModelStackWithAutoParam* msp, Domain domain, int32_t value) {
	if (domain == Domain::SYNTH) {
		return std::clamp<int32_t>(msp->paramCollection->paramValueToKnobPos(value, msp) + 64, 0,
		                           (int32_t)kMaxValueSynth);
	}
	return bigValueToCC(value);
}

// Writes one destination live on the clip: MIDI routes through processParamFromInputMIDIChannel
// (reaches the output and records like a manual CC change); SYNTH resolves the param and writes it
// with the same region/cloning logic, via setValuePossiblyForRegion (which notifies the Sound).
static void writeDestLive(MelodicInstrument* instrument, Clip* clip, ModelStackWithTimelineCounter* modelStack,
                          Domain domain, uint8_t dest, int32_t units) {
	if (domain == Domain::MIDI) {
		instrument->processParamFromInputMIDIChannel(dest, (units - 64) << 25, modelStack);
		return;
	}
	int32_t modPos = 0;
	int32_t modLength = 0;
	if (modelStack->timelineCounterIsSet()) {
		modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);
		clip = (Clip*)modelStack->getTimelineCounter(); // may have been cloned for arrangement recording
		// Only if this exact TimelineCounter is having automation step-edited can we set the value
		// for just a region - same rule as processParamFromInputMIDIChannel.
		if (view.modLength
		    && modelStack->getTimelineCounter() == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
			modPos = view.modPos;
			modLength = view.modLength;
		}
	}
	params::Kind kind;
	int32_t id;
	decodeSynthDestination(dest, &kind, &id);
	ModelStackWithAutoParam* msp = instrument->getModelStackWithParam(modelStack, clip, id, kind, true, false);
	if (msp && msp->autoParam) {
		msp->autoParam->setValuePossiblyForRegion(unitsToParamValue(msp, domain, units), msp, modPos, modLength);
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

// Everything the bake machinery needs to resolve destinations on one clip, in either domain. On
// MIDI clips coll/summary point at the clip's MIDIParamCollection; on synths destinations live in
// the clip's fixed patched/unpatched param sets instead (always present, never reallocated).
struct FanContext {
	MelodicInstrument* instrument = nullptr;
	Clip* clip = nullptr;
	Domain domain = Domain::MIDI;
	MIDIParamCollection* coll = nullptr;       // MIDI only
	ParamCollectionSummary* summary = nullptr; // MIDI only
};

// Fills the context; false if the clip isn't macro-capable (or a MIDI clip's params are missing).
static bool getFanContext(Clip* clip, FanContext* ctx) {
	ctx->instrument = macroClipInstrument(clip);
	if (!ctx->instrument) {
		return false;
	}
	ctx->clip = clip;
	ctx->domain = domainForOutput(clip->output);
	if (ctx->domain == Domain::MIDI) {
		ctx->coll = getMIDIParams(clip, &ctx->summary);
		if (!ctx->coll) {
			return false;
		}
	}
	return true;
}

// Resolves destination `dest`'s AutoParam on the context's clip into a full model stack, or null
// if it doesn't exist (MIDI CC params are created on demand; synth params always exist). A cascade
// id resolves to the downstream macro's lane param in either domain.
static ModelStackWithAutoParam* destParamStack(const FanContext& ctx, ModelStackWithThreeMainThings* three,
                                               uint8_t dest) {
	if (ctx.domain == Domain::MIDI) {
		MIDIParam* param = ctx.coll->params.getParamFromCC(dest);
		if (!param) {
			return nullptr;
		}
		return three->addParamCollectionAndId(ctx.coll, ctx.summary, dest)->addAutoParam(&param->param);
	}
	params::Kind kind;
	int32_t id;
	decodeSynthDestination(dest, &kind, &id);
	ModelStackWithAutoParam* msp =
	    (kind == params::Kind::PATCHED) ? three->getPatchedAutoParamFromId(id) : three->getUnpatchedAutoParamFromId(id);
	return (msp != nullptr && msp->autoParam != nullptr) ? msp : nullptr;
}

// Macro macroIndex's own lane AutoParam on this clip, or null if it was never created (MIDI only -
// synth lanes are fixed array slots that always exist).
static AutoParam* laneParam(const FanContext& ctx, int32_t macroIndex) {
	if (ctx.domain == Domain::MIDI) {
		MIDIParam* param = ctx.coll->params.getParamFromCC(paramIDForMacro(macroIndex));
		return param ? &param->param : nullptr;
	}
	UnpatchedParamSet* unpatched = ctx.clip->paramManager.getUnpatchedParamSet();
	return unpatched ? &unpatched->params[params::UNPATCHED_MACRO_1 + macroIndex] : nullptr;
}

// Whether the macro's lane was ever touched on this clip - the gate for every re-fan: an untouched
// lane never baked anything, so config changes must leave the (possibly live-recorded) targets
// alone. MIDI: the pseudo-CC param exists. Synth: the always-present lane param is automated or
// off its setup default (knobPos 0).
static bool laneEverTouched(const FanContext& ctx, int32_t macroIndex) {
	if (ctx.domain == Domain::MIDI) {
		return ctx.coll->params.getParamFromCC(paramIDForMacro(macroIndex)) != nullptr;
	}
	AutoParam* lane = laneParam(ctx, macroIndex);
	return lane != nullptr && lane->containsSomething(0x80000000u);
}

// Clears one target's lane and, when sending, mirrors the source lane into it scaled by [from, to].
// On MIDI clips the caller must not create any params between resolving `source` and this call: an
// insert moves the vector's storage and would dangle the pointer.
static void bakeTarget(const FanContext& ctx, ModelStackWithThreeMainThings* three, const MacroTargetSlot& target,
                       AutoParam* source, Action* action) {
	ModelStackWithAutoParam* modelStackWithParam = destParamStack(ctx, three, target.cc);
	if (!modelStackWithParam) {
		return; // never existed (MIDI): nothing to clear, and nothing will be written (send off)
	}

	// Overwrite: clear first (deleteAutomation snapshots into `action` for undo when given).
	modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam, false);

	if (target.send) {
		int32_t outMax = maxTargetValue(ctx.domain);
		for (int32_t n = 0; n < source->nodes.getNumElements(); n++) {
			ParamNode* node = source->nodes.getElement(n);
			int32_t out = scaleTarget(target, bigValueToCC(node->value), outMax);
			modelStackWithParam->autoParam->setNodeAtPos(
			    node->pos, unitsToParamValue(modelStackWithParam, ctx.domain, out), node->interpolated);
		}
		// Base value for regions before the first node / when the source has no nodes.
		int32_t base = scaleTarget(target, bigValueToCC(source->getCurrentValue()), outMax);
		modelStackWithParam->autoParam->setCurrentValueBasicForSetup(
		    unitsToParamValue(modelStackWithParam, ctx.domain, base));
	}
}

static void refreshAutomationGridIfShowingDest(Clip* clip, Domain domain, uint8_t dest) {
	if (getRootUI() != &automationView || automationView.onArrangerView) {
		return;
	}
	if (domain == Domain::SYNTH) {
		// synth lanes are tracked by (kind, id); a cascade id maps to the downstream lane param
		params::Kind kind;
		int32_t id;
		decodeSynthDestination(dest, &kind, &id);
		if (clip->lastSelectedParamKind == kind && clip->lastSelectedParamID == id) {
			uiNeedsRendering(&automationView);
		}
		return;
	}
	// MIDI clips track the selected lane by CC number alone (lastSelectedParamKind stays unset)
	if (clip->lastSelectedParamID == dest) {
		uiNeedsRendering(&automationView);
	}
}

static void sendToTargets(MelodicInstrument* instrument, Clip* clip, ModelStackWithTimelineCounter* modelStack,
                          Macro& macro, int32_t value) {
	Domain domain = domainForOutput(clip->output);
	int32_t macroIndex = (int32_t)(&macro - instrument->macros);
	// Mirror the raw source value into the macro's own automation lane so the lane tracks the
	// source live - and records its moves like any param. On MIDI clips the lane is a pseudo-CC
	// param that never emits MIDI (sendMIDI suppresses paramIDs >= kMacroParamIDBase); on synths
	// it is the UNPATCHED_MACRO_n param, which nothing in the render code reads. The targets get
	// the scaled values below.
	writeDestLive(instrument, clip, modelStack, domain, laneDestination(domain, macroIndex), value);
	refreshAutomationGridIfShowingDest(clip, domain, laneDestination(domain, macroIndex));
	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		MacroTargetSlot& target = macro.targets[slot];
		if (target.cc == kTargetCCNone || !target.send) {
			continue;
		}
		// A shadowed target (another target owns its CC) is inert - only the owner drives.
		if (isTargetShadowed(instrument->macros, macroIndex, slot)) {
			continue;
		}
		// A cascade target drives the downstream macro itself: the scaled value becomes its input
		// and fans out again through its own targets. Recursion is bounded (higher indices only);
		// an inactive downstream macro mutes its whole branch live, same as a source-driven macro.
		// The cascade input is always source units 0..127 regardless of domain.
		if (isMacroParamID(target.cc)) {
			Macro& downstream = instrument->macros[macroIndexFromParamID(target.cc)];
			if (downstream.active) {
				sendToTargets(instrument, clip, modelStack, downstream, scaleTarget(target, value, kMaxValue));
			}
			continue;
		}
		int32_t out = scaleTarget(target, value, maxTargetValue(domain));
		// writes the value into the clip's own destination param, so it reaches the output/sound
		// and gets recorded into that destination's automation lane like a manual change would
		writeDestLive(instrument, clip, modelStack, domain, target.cc, out);
		refreshAutomationGridIfShowingDest(clip, domain, target.cc);
	}
}

// True when an ACTIVE macro's owned cascade target feeds macros[m]'s input. The driven macro's own
// source is shadowed then - exactly one thing feeds each macro, like one driver per CC.
static bool macroInputDriven(const Macro* macros, int32_t m) {
	int32_t ownerSlot = 0;
	int32_t owner = findCCOwner(macros, (uint8_t)paramIDForMacro(m), &ownerSlot);
	return owner >= 0 && macros[owner].active;
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
	MelodicInstrument* instrument = macroClipInstrument(clip);
	if (!instrument) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	bool matched = false;
	for (int32_t m = 0; m < kNumMacros; m++) {
		Macro& macro = instrument->macros[m];
		// a macro fed by an upstream cascade target ignores its own source (one feed per macro)
		if (!macro.active || macroInputDriven(instrument->macros, m)) {
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
	MelodicInstrument* instrument = macroClipInstrument(clip);
	if (!instrument) {
		return false;
	}
	// Cheap pre-check so a normal (non-source) knob turn bails before building a modelStack. A
	// macro fed by an upstream cascade target ignores its own source (one feed per macro).
	bool anyMatch = false;
	for (int32_t m = 0; m < kNumMacros; m++) {
		Macro& macro = instrument->macros[m];
		if (macro.active && macro.sourceKnob == whichKnob && !macroInputDriven(instrument->macros, m)) {
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

	for (int32_t m = 0; m < kNumMacros; m++) {
		Macro& macro = instrument->macros[m];
		if (!macro.active || macro.sourceKnob != whichKnob || macroInputDriven(instrument->macros, m)) {
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
	// The dedicated macro row is a MIDI-clip-only feature: on synth clips every mod-knob row
	// already has a real function, so none is repurposed for macros.
	Clip* midiClip = (clip && clip->output && clip->output->type == OutputType::MIDI_OUT) ? clip : nullptr;
	MelodicInstrument* instrument = macroClipInstrument(midiClip);
	if (!instrument) {
		return false;
	}
	Macro& macro = instrument->macros[whichKnob]; // knob 1 -> Macro 1, knob 2 -> Macro 2
	if (!macro.active) {
		return false; // inactive macro: let the knob keep its normal behavior
	}
	if (macroInputDriven(instrument->macros, whichKnob)) {
		return false; // fed by an upstream cascade target: its own source is shadowed
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
	if (!memcmp(tagName, MACRO_TARGET_PREFIX, prefixLen) && tagName[prefixLen] >= '1'
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
			char tagName[10] = MACRO_TARGET_PREFIX "1"; // "target1"
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

	writer.writeOpeningTagBeginning(MACRO_ELEMENT_TAG);
	writer.writeAttribute("active", macro.active ? 1 : 0, false);
	if (!macro.name.isEmpty()) {
		writer.writeAttribute("name", macro.name.get(), false); // optional user label
	}
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

	macro.source.writeCCToFile(writer, MACRO_SOURCE_TAG);

	writeTargetsToFile(writer, macro);

	writer.writeClosingTag(MACRO_ELEMENT_TAG);
}

// Writes the <macros> block holding all four macros. Called from
// MIDIInstrument::writeDataToFile with the instrument's own macros/enable state.
void writeMacrosToFile(Serializer& writer, Macro* macros) {
	writer.writeOpeningTagBeginning(MACROS_TAG);
	writer.writeOpeningTagEnd();

	for (int32_t m = 0; m < kNumMacros; m++) {
		writeMacroToFile(writer, macros[m]);
	}

	writer.writeClosingTag(MACROS_TAG);
}

// Reads a target's cc/from/to/send attributes (e.g. <target1 cc="42" from="0" to="100" send="1" />).
static void readTargetFromFile(Deserializer& reader, MacroTargetSlot& target, int32_t macroIndex) {
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
	// a real CC, or a cascade destination (higher-indexed macro only - keeps the graph acyclic)
	target.cc = isValidTargetDestination(cc, macroIndex) ? cc : kTargetCCNone;
	target.from = std::clamp<int32_t>(from, 0, kMaxValue);
	target.to = std::clamp<int32_t>(to, 0, kMaxValue);
	target.send = send;
}

static void readMacroFromFile(Deserializer& reader, Macro& macro, int32_t macroIndex) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "active")) {
			macro.active = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, "name")) {
			reader.readTagOrAttributeValueString(&macro.name);
		}
		else if (!strcmp(tagName, "sourceKnob")) {
			int32_t k = reader.readTagOrAttributeValueInt();
			macro.sourceKnob = (k >= 0 && k < kNumPhysicalModKnobs) ? (int8_t)k : -1;
		}
		else if (!strcmp(tagName, MACRO_SOURCE_TAG)) {
			macro.source.readCCFromFile(reader);
		}
		else {
			int32_t slot = targetSlotFromTagName(tagName);
			if (slot >= 0) {
				readTargetFromFile(reader, macro.targets[slot], macroIndex);
			}
		}
		reader.exitTag();
	}
}

// Reads the children of a <macros> block: the per-instrument enable attribute and the four
// positional <macro> elements. The caller (MIDIInstrument::readTagFromFile) exits the outer tag.
void readMacrosFromFile(Deserializer& reader, Macro* macros) {
	char const* tagName;
	int32_t macroIndex = 0;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, MACRO_ELEMENT_TAG) && macroIndex < kNumMacros) {
			readMacroFromFile(reader, macros[macroIndex], macroIndex);
			macroIndex++;
		}
		reader.exitTag();
	}
}

// Writes a preset: targets only (<macroPreset><target1..8/></macroPreset>). A preset is a portable
// target configuration - it deliberately omits the source and the macro's active state so it can
// be applied to any macro. The caller (save browser) owns createXMLFile()/closeFileAfterWriting().
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex) {
	writer.writeOpeningTagBeginning(MACRO_PRESET_TAG);
	writer.writeOpeningTagEnd();
	writeTargetsToFile(writer, macros[macroIndex]);
	writer.writeClosingTag(MACRO_PRESET_TAG);
}

// Loads a preset's targets into macros[macroIndex], replacing that macro's targets only and
// leaving its source and active state untouched. Loaded CCs that another macro also targets
// resolve by ownership rank; the fan-out below only bakes the slots this macro owns.
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex) {
	Error error = StorageManager::openXMLFile(fp, smDeserializer, MACRO_PRESET_TAG);
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
			readTargetFromFile(reader, macros[macroIndex].targets[slot], macroIndex);
		}
		reader.exitTag();
	}
	activeDeserializer->closeWriter();

	// Clear baked lanes on destinations the preset dropped, then re-bake the macro lane into the
	// new set.
	FanContext ctx;
	if (getFanContext(clip, &ctx) && laneEverTouched(ctx, macroIndex)) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		ModelStackWithThreeMainThings* three =
		    modelStack->addTimelineCounter(clip)
		        ->addNoteRow(0, nullptr)
		        ->addOtherTwoThings(ctx.instrument->toModControllable(), &clip->paramManager);
		for (int32_t f = 0; f < kNumTargetSlots; f++) {
			uint8_t oldCC = oldCCs[f];
			if (oldCC == kTargetCCNone || findTargetCCOwner(macros, oldCC, -1, -1) >= 0) {
				continue; // dropped destination still targeted by some target - its bake stays
			}
			ModelStackWithAutoParam* oldParamStack = destParamStack(ctx, three, oldCC);
			if (oldParamStack) {
				oldParamStack->autoParam->deleteAutomation(nullptr, oldParamStack, false);
				refreshAutomationGridIfShowingDest(clip, ctx.domain, oldCC);
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
	FanContext ctx;
	if (!getFanContext(clip, &ctx)) {
		return false;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithThreeMainThings* three =
	    modelStack->addTimelineCounter(clip)
	        ->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(ctx.instrument->toModControllable(), &clip->paramManager);

	bool changed = false;
	for (MacroTargetSlot& target : ctx.instrument->macros[macroIndex].targets) {
		if (target.cc == kTargetCCNone) {
			continue;
		}
		// Read this destination's current value on THIS clip - the inverse of the write in
		// sendToTargets(). On MIDI, a CC that's never been moved/recorded here has no param (and no
		// known value): leave its endpoint alone rather than capturing a made-up default. Synth
		// params always exist, so every configured target captures.
		if (ctx.domain == Domain::MIDI) {
			AutoParam* param = nullptr;
			MIDIParam* midiParam = ctx.coll->params.getParamFromCC(target.cc);
			if (midiParam) {
				param = &midiParam->param;
			}
			else {
				int32_t dimension = expressionDimensionFromCC(target.cc);
				ExpressionParamSet* expression =
				    (dimension >= 0) ? clip->paramManager.getExpressionParamSet() : nullptr;
				if (expression) {
					param = &expression->params[dimension];
				}
			}
			if (!param) {
				continue;
			}
			(toMax ? target.to : target.from) = (uint8_t)bigValueToCC(param->getCurrentValue());
		}
		else {
			ModelStackWithAutoParam* msp = destParamStack(ctx, three, target.cc);
			if (!msp) {
				continue;
			}
			(toMax ? target.to : target.from) =
			    (uint8_t)paramValueToUnits(msp, ctx.domain, msp->autoParam->getCurrentValue());
		}
		changed = true;
	}
	if (changed) {
		ctx.instrument->editedByUser = true;
	}
	return changed;
}

// Creates every MIDI param a fan-out of macroIndex will write - including the lanes and targets of
// any cascaded downstream macros - BEFORE any AutoParam pointers are taken: an insert
// memmoves/reallocs the vector's element storage, which would dangle them. MIDI-domain only: synth
// param sets are fixed arrays that never reallocate, so there is nothing to pre-create. Recursion
// is bounded: cascade targets only point at higher-indexed macros.
static void preCreateFanOutParams(const FanContext& ctx, int32_t macroIndex) {
	if (ctx.domain != Domain::MIDI) {
		return;
	}
	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		MacroTargetSlot& target = ctx.instrument->macros[macroIndex].targets[slot];
		if (target.cc == kTargetCCNone || !target.send || isTargetShadowed(ctx.instrument->macros, macroIndex, slot)) {
			continue;
		}
		ctx.coll->params.getOrCreateParamFromCC(target.cc, 0);
		if (isMacroParamID(target.cc)) {
			preCreateFanOutParams(ctx, macroIndexFromParamID(target.cc));
		}
	}
}

static void fanOutMacroLane(const FanContext& ctx, int32_t macroIndex, ModelStackWithTimelineCounter* modelStack,
                            Action* action) {
	// If the macro lane was never touched on this clip, nothing was ever baked - leave the
	// targets (possibly live-recorded) alone.
	if (!laneEverTouched(ctx, macroIndex)) {
		return;
	}

	preCreateFanOutParams(ctx, macroIndex);
	AutoParam* source = laneParam(ctx, macroIndex);
	if (!source) {
		return;
	}

	ModelStackWithThreeMainThings* three =
	    modelStack->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(ctx.instrument->toModControllable(), &ctx.clip->paramManager);

	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		MacroTargetSlot& target = ctx.instrument->macros[macroIndex].targets[slot];
		if (target.cc == kTargetCCNone) {
			continue;
		}
		// A shadowed target's lane belongs to its owner - don't clear or bake it.
		if (isTargetShadowed(ctx.instrument->macros, macroIndex, slot)) {
			continue;
		}
		bakeTarget(ctx, three, target, source, action);
		refreshAutomationGridIfShowingDest(ctx.clip, ctx.domain, target.cc);
		// A cascade target's lane just changed: re-derive the downstream macro's own fan-out from
		// it. Everything the recursion writes was pre-created above, so `source` stays valid.
		if (isMacroParamID(target.cc) && (target.send || laneEverTouched(ctx, macroIndexFromParamID(target.cc)))) {
			fanOutMacroLane(ctx, macroIndexFromParamID(target.cc), modelStack, action);
		}
	}
	ctx.instrument->editedByUser = true;
}

void reFanMacro(Clip* clip, int32_t macroIndex, Action* action) {
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	FanContext ctx;
	if (!getFanContext(clip, &ctx)) {
		return;
	}
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
	fanOutMacroLane(ctx, macroIndex, modelStackWithTimelineCounter, action);
}

void reFanTarget(Clip* clip, int32_t macroIndex, int32_t slot, Action* action) {
	if (getCurrentUI() == &loadSongUI) {
		return;
	}
	FanContext ctx;
	if (!getFanContext(clip, &ctx)) {
		return;
	}
	MacroTargetSlot& target = ctx.instrument->macros[macroIndex].targets[slot];
	if (target.cc == kTargetCCNone) {
		return;
	}
	// A shadowed target's lane belongs to its owner - don't clear or bake it.
	if (isTargetShadowed(ctx.instrument->macros, macroIndex, slot)) {
		return;
	}
	if (!laneEverTouched(ctx, macroIndex)) {
		return; // macro lane never touched on this clip - nothing baked, nothing to redo
	}
	if (target.send && ctx.domain == Domain::MIDI) {
		ctx.coll->params.getOrCreateParamFromCC(target.cc, 0); // create before taking the source pointer
		if (isMacroParamID(target.cc)) {
			// the cascade below this target must also be pre-created before pointers are taken
			preCreateFanOutParams(ctx, macroIndexFromParamID(target.cc));
		}
	}
	AutoParam* source = laneParam(ctx, macroIndex);
	if (!source) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
	ModelStackWithThreeMainThings* three =
	    modelStackWithTimelineCounter->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(ctx.instrument->toModControllable(), &clip->paramManager);
	bakeTarget(ctx, three, target, source, action);
	refreshAutomationGridIfShowingDest(clip, ctx.domain, target.cc);
	// A cascade target's lane just changed: re-derive the downstream macro's fan-out from it.
	if (isMacroParamID(target.cc) && (target.send || laneEverTouched(ctx, macroIndexFromParamID(target.cc)))) {
		fanOutMacroLane(ctx, macroIndexFromParamID(target.cc), modelStackWithTimelineCounter, action);
	}
	ctx.instrument->editedByUser = true;
}

void changeTargetCC(Clip* clip, int32_t macroIndex, int32_t slot, uint8_t newCC) {
	FanContext ctx;
	if (!getFanContext(clip, &ctx)) {
		return;
	}
	// Cascade rule: a macro may only target HIGHER-indexed macros (keeps the graph acyclic), and a
	// synth destination byte must be a real targetable param (never a lane byte). The pickers never
	// offer invalid ids, but this is the single reassignment primitive - guard it.
	if (newCC != kTargetCCNone && !isValidTargetDestination(ctx.domain, newCC, macroIndex)) {
		return;
	}
	MacroTargetSlot& target = ctx.instrument->macros[macroIndex].targets[slot];
	uint8_t oldCC = target.cc;
	if (newCC == oldCC) {
		return;
	}
	target.cc = newCC;
	ctx.instrument->editedByUser = true;

	if (!laneEverTouched(ctx, macroIndex)) {
		return; // no bake ever happened on this clip: pure config change
	}

	// One undoable action for the pair of lane writes; consecutive detents of a scroll gesture join it.
	Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::ALLOWED);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
	ModelStackWithThreeMainThings* three =
	    modelStackWithTimelineCounter->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(ctx.instrument->toModControllable(), &clip->paramManager);

	// Deal with the destination we left: if another target still targets it, hand ownership to
	// whichever one now ranks first (it may have been shadowed by us - re-bake it); otherwise clear
	// our bake so it doesn't play as a ghost lane. target.cc is already reassigned, so findCCOwner
	// excludes us.
	if (oldCC != kTargetCCNone) {
		int32_t ownerSlot = 0;
		int32_t ownerMacro = findCCOwner(ctx.instrument->macros, oldCC, &ownerSlot);
		ModelStackWithAutoParam* oldParamStack = destParamStack(ctx, three, oldCC);
		if (oldParamStack) {
			oldParamStack->autoParam->deleteAutomation(action, oldParamStack, false);
			refreshAutomationGridIfShowingDest(clip, ctx.domain, oldCC);
		}
		if (ownerMacro >= 0) {
			reFanTarget(clip, ownerMacro, ownerSlot, action); // promote the new owner's bake
		}
		// Departing a cascade destination with no owner left: its lane was just cleared, so
		// re-derive the downstream macro's fan-out from the now-empty lane (clears its branch).
		else if (isMacroParamID(oldCC) && oldParamStack) {
			fanOutMacroLane(ctx, macroIndexFromParamID(oldCC), modelStackWithTimelineCounter, action);
		}
	}

	// Bake the macro curve into the new destination - unless another target outranks us there
	// (we're shadowed: keep the config but leave the owner's lane alone; the UI shows the conflict
	// and keeps our LED off).
	if (newCC != kTargetCCNone && findShadowingOwner(ctx.instrument->macros, newCC, macroIndex, slot) < 0) {
		if (target.send && ctx.domain == Domain::MIDI) {
			ctx.coll->params.getOrCreateParamFromCC(newCC, 0); // create before taking the source pointer
			if (isMacroParamID(newCC)) {
				// the cascade below the new destination must be pre-created before pointers are taken
				preCreateFanOutParams(ctx, macroIndexFromParamID(newCC));
			}
		}
		AutoParam* source = laneParam(ctx, macroIndex);
		if (source) {
			bakeTarget(ctx, three, target, source, action);
			refreshAutomationGridIfShowingDest(clip, ctx.domain, newCC);
			// A cascade destination's lane just changed: re-derive its macro's fan-out from it.
			if (isMacroParamID(newCC) && (target.send || laneEverTouched(ctx, macroIndexFromParamID(newCC)))) {
				fanOutMacroLane(ctx, macroIndexFromParamID(newCC), modelStackWithTimelineCounter, action);
			}
		}
	}

	view.setModLedStates(); // the mod-button LEDs show target assignment while a macro lane is shown
}

void setMacroActive(Clip* clip, int32_t macroIndex, bool active) {
	FanContext ctx;
	if (!getFanContext(clip, &ctx)) {
		return;
	}
	Macro* macros = ctx.instrument->macros;
	if (macros[macroIndex].active == active) {
		return;
	}

	// Ownership ranks active macros above inactive ones, so flipping `active` can hand this macro's
	// duplicated destinations to/from another targeter. Record the pre-flip owners to detect the
	// transfers.
	int32_t oldOwnerMacros[kNumTargetSlots];
	int32_t oldOwnerSlots[kNumTargetSlots];
	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		oldOwnerSlots[slot] = 0;
		oldOwnerMacros[slot] = findCCOwner(macros, macros[macroIndex].targets[slot].cc, &oldOwnerSlots[slot]);
	}

	macros[macroIndex].active = active;
	ctx.instrument->editedByUser = true;

	Action* action = nullptr;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithThreeMainThings* three =
	    modelStack->addTimelineCounter(clip)
	        ->addNoteRow(0, nullptr)
	        ->addOtherTwoThings(ctx.instrument->toModControllable(), &clip->paramManager);

	for (int32_t slot = 0; slot < kNumTargetSlots; slot++) {
		uint8_t cc = macros[macroIndex].targets[slot].cc;
		if (cc == kTargetCCNone) {
			continue;
		}
		// duplicates of the destination within this macro share one lane - handle each once
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
		// The destination changed hands. Clear the old owner's bake first (only if that macro ever
		// baked - same ghost-lane rule as changeTargetCC), then promote the new owner's; if the new
		// owner's lane was never automated, the destination simply stays clear.
		if (oldOwnerMacros[slot] >= 0 && laneEverTouched(ctx, oldOwnerMacros[slot])) {
			ModelStackWithAutoParam* modelStackWithParam = destParamStack(ctx, three, cc);
			if (modelStackWithParam) {
				if (!action) {
					action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
				}
				modelStackWithParam->autoParam->deleteAutomation(action, modelStackWithParam, false);
				refreshAutomationGridIfShowingDest(clip, ctx.domain, cc);
			}
		}
		if (!action) {
			action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
		}
		reFanTarget(clip, newOwnerMacro, newOwnerSlot, action);
	}
}

} // namespace Macros
