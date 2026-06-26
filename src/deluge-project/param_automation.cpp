/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/param_automation.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"  // intToHex
#include "util/functions.h" // sourceToString
#include <algorithm>
#include <cstdint>

namespace deluge::project {

DocAutoParam toDocAutoParam(const AutoParam& param) {
	return toDocAutoParam(param.currentValue, param.nodes);
}

void fromDocAutoParam(const DocAutoParam& doc, AutoParam& param) {
	fromDocAutoParam(doc, param.currentValue, param.nodes);
}

DocParamSet toDocParamSet(deluge::modulation::params::Kind kind, const AutoParam* params, int32_t numParams) {
	DocParamSet doc;
	doc.kind = kind;
	doc.params.reserve(numParams);
	for (int32_t p = 0; p < numParams; p++) {
		doc.params.push_back(toDocAutoParam(params[p]));
	}
	return doc;
}

void fromDocParamSet(const DocParamSet& doc, AutoParam* params, int32_t numParams) {
	int32_t count = std::min(static_cast<int32_t>(doc.params.size()), numParams);
	for (int32_t p = 0; p < count; p++) {
		fromDocAutoParam(doc.params[p], params[p]);
	}
}

DocParamSet toDocParamSet(ParamSet& paramSet) {
	return toDocParamSet(paramSet.getParamKind(), paramSet.params, paramSet.getNumParams());
}

void fromDocParamSet(const DocParamSet& doc, ParamSet& paramSet) {
	fromDocParamSet(doc, paramSet.params, paramSet.getNumParams());
}

namespace params = deluge::modulation::params;

namespace {
using deluge::modulation::params::Kind;
// Slot 0 holds either the unpatched ParamSet or a MIDI-CC collection (the latter deferred to a later
// sub-phase). The unpatched set's own `kind` is often still NONE here (the owner sets UNPATCHED_SOUND/
// GLOBAL later), so identify it positionally: present and not the MIDI collection.
bool isUnpatchedSetSlot(ParamCollection* c) {
	return c != nullptr && c->getParamKind() != Kind::MIDI;
}
} // namespace

DocParamManager toDocParamManager(ParamManager& paramManager) {
	DocParamManager doc;
	// Fixed slot layout: [0] unpatched (or MIDI, deferred), [1] patched, [2] patch cables (deferred),
	// expression at expressionParamSetOffset.
	if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
		doc.unpatched = toDocParamSet(*static_cast<ParamSet*>(c));
	}
	if (ParamCollection* c = paramManager.summaries[1].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCHED) {
		doc.patched = toDocParamSet(*static_cast<ParamSet*>(c));
	}
	if (paramManager.getExpressionParamSetOffset() != 0) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			doc.expression = toDocParamSet(*e);
		}
	}
	// Slot [2]: patch cables. Copy each cable's saved fields verbatim (the ParamDescriptor encodes the
	// destination + any range-cable nesting); rangeAdjustmentPointer is runtime and rebuilt on projection.
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		PatchCableSet* pcs = static_cast<PatchCableSet*>(c);
		doc.patch_cables.reserve(pcs->numPatchCables);
		for (int32_t i = 0; i < pcs->numPatchCables; i++) {
			PatchCable& cable = pcs->patchCables[i];
			doc.patch_cables.push_back(DocPatchCable{
			    .from = cable.from,
			    .destination = cable.destinationParamDescriptor,
			    .polarity = cable.polarity,
			    .amount = toDocAutoParam(cable.param),
			});
		}
	}
	return doc;
}

void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager) {
	if (doc.unpatched) {
		if (ParamCollection* c = paramManager.summaries[0].paramCollection; isUnpatchedSetSlot(c)) {
			fromDocParamSet(*doc.unpatched, *static_cast<ParamSet*>(c));
		}
	}
	if (doc.patched) {
		if (ParamCollection* c = paramManager.summaries[1].paramCollection;
		    c != nullptr && c->getParamKind() == Kind::PATCHED) {
			fromDocParamSet(*doc.patched, *static_cast<ParamSet*>(c));
		}
	}
	if (doc.expression) {
		if (ExpressionParamSet* e = paramManager.getExpressionParamSet(); e != nullptr) {
			fromDocParamSet(*doc.expression, *e);
		}
	}
	// Patch cables: refill the cable set faithfully (count + each saved field). setupPatching (the
	// engine's post-load step) rebuilds the runtime rangeAdjustmentPointers.
	if (ParamCollection* c = paramManager.summaries[2].paramCollection;
	    c != nullptr && c->getParamKind() == Kind::PATCH_CABLE) {
		PatchCableSet* pcs = static_cast<PatchCableSet*>(c);
		int32_t count = std::min(static_cast<int32_t>(doc.patch_cables.size()), int32_t{kMaxNumPatchCables});
		pcs->numPatchCables = count;
		for (int32_t i = 0; i < count; i++) {
			const DocPatchCable& dc = doc.patch_cables[i];
			pcs->patchCables[i].from = dc.from;
			pcs->patchCables[i].destinationParamDescriptor = dc.destination;
			pcs->patchCables[i].polarity = dc.polarity;
			fromDocAutoParam(dc.amount, pcs->patchCables[i].param);
		}
	}
}

// --- Generic serialization (Phase 4) ---

void writeDocAutoParam(const DocAutoParam& doc, Serializer& out, bool writeAutomation,
                       const int32_t* valueForOverride) {
	// Mirrors AutoParam::writeToFile: "0x" + value(8 hex) + per-node value(8 hex) + pos|interpolated<<31
	// (8 hex). When overriding an automated param, the leading value is the override, not current_value.
	char buffer[9];
	out.write("0x");
	int32_t valueNow = (valueForOverride && !doc.nodes.empty()) ? *valueForOverride : doc.current_value;
	intToHex(valueNow, buffer);
	out.write(buffer);
	if (writeAutomation) {
		for (const ParamNode& node : doc.nodes) {
			intToHex(node.value, buffer);
			out.write(buffer);
			uint32_t pos = node.pos;
			if (node.interpolated) {
				pos |= static_cast<uint32_t>(1) << 31;
			}
			intToHex(pos, buffer);
			out.write(buffer);
		}
	}
}

void writeDocParamAsAttribute(const DocParamSet& doc, char const* name, int32_t p, Serializer& out,
                              bool writeAutomation, bool onlyIfContainsSomething, const int32_t* valuesForOverride) {
	// Byte-for-byte reproduction of ParamSet::writeParamAsAttribute.
	const DocAutoParam& param = doc.params[p];
	// containsSomething(neutral=0): automated, or currentValue != 0 (as an unsigned compare).
	if (onlyIfContainsSomething && param.nodes.empty() && static_cast<uint32_t>(param.current_value) == 0u) {
		return;
	}
	const int32_t* valueForOverride = valuesForOverride ? &valuesForOverride[p] : nullptr;
	out.insertCommaIfNeeded();
	out.write("\n");
	out.printIndents();
	out.writeTagNameAndSeperator(name);
	out.write("\"");
	writeDocAutoParam(param, out, writeAutomation, valueForOverride);
	out.write("\"");
}

namespace {
// The ModControllableAudio shared param block (mod_controllable_audio.cpp): the flat attribute
// sequence + the <equalizer> group, in file order. All written unconditionally (onlyIfContainsSomething
// is false in the engine for every one).
// The first six ModControllableAudio attrs honour the value-override array; the engine passes
// valuesForOverride only to these (mod_controllable_audio.cpp).
constexpr DocParamAttr kModControllableOverrideAttrs[] = {
    {"stutterRate", params::UNPATCHED_STUTTER_RATE},
    {"sampleRateReduction", params::UNPATCHED_SAMPLE_RATE_REDUCTION},
    {"bitCrush", params::UNPATCHED_BITCRUSHING},
    {"modFXOffset", params::UNPATCHED_MOD_FX_OFFSET},
    {"modFXFeedback", params::UNPATCHED_MOD_FX_FEEDBACK},
    {"compressorThreshold", params::UNPATCHED_COMPRESSOR_THRESHOLD},
};
// The remaining ModControllableAudio attrs (arp / probability / spread) are written without override —
// the engine omits valuesForOverride for these.
constexpr DocParamAttr kModControllableArpAttrs[] = {
    {"arpeggiatorGate", params::UNPATCHED_ARP_GATE},
    {"noteProbability", params::UNPATCHED_NOTE_PROBABILITY},
    {"bassProbability", params::UNPATCHED_ARP_BASS_PROBABILITY},
    {"swapProbability", params::UNPATCHED_ARP_SWAP_PROBABILITY},
    {"glideProbability", params::UNPATCHED_ARP_GLIDE_PROBABILITY},
    {"reverseProbability", params::UNPATCHED_REVERSE_PROBABILITY},
    {"chordProbability", params::UNPATCHED_ARP_CHORD_PROBABILITY},
    {"ratchetProbability", params::UNPATCHED_ARP_RATCHET_PROBABILITY},
    {"ratchetAmount", params::UNPATCHED_ARP_RATCHET_AMOUNT},
    {"sequenceLength", params::UNPATCHED_ARP_SEQUENCE_LENGTH},
    {"chordPolyphony", params::UNPATCHED_ARP_CHORD_POLYPHONY},
    {"rhythm", params::UNPATCHED_ARP_RHYTHM},
    {"spreadVelocity", params::UNPATCHED_SPREAD_VELOCITY},
    {"spreadGate", params::UNPATCHED_ARP_SPREAD_GATE},
    {"spreadOctave", params::UNPATCHED_ARP_SPREAD_OCTAVE},
};
constexpr DocParamAttr kEqualizerGroup[] = {
    {"bass", params::UNPATCHED_BASS},
    {"treble", params::UNPATCHED_TREBLE},
    {"bassFrequency", params::UNPATCHED_BASS_FREQ},
    {"trebleFrequency", params::UNPATCHED_TREBLE_FREQ},
};
} // namespace

void writeDocParamGroup(Serializer& out, const char* groupName, std::span<const DocParamAttr> params_,
                        const DocParamSet& doc, bool writeAutomation, const int32_t* valuesForOverride) {
	out.writeOpeningTagBeginning(groupName);
	for (const DocParamAttr& a : params_) {
		writeDocParamAsAttribute(doc, a.name, a.param_id, out, writeAutomation, false, valuesForOverride);
	}
	out.closeTag();
}

void serializeModControllableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                             const int32_t* valuesForOverride) {
	for (const DocParamAttr& a : kModControllableOverrideAttrs) {
		writeDocParamAsAttribute(unpatched, a.name, a.param_id, out, writeAutomation, false, valuesForOverride);
	}
	// The arp/probability/spread attrs are always written without the override (engine parity).
	for (const DocParamAttr& a : kModControllableArpAttrs) {
		writeDocParamAsAttribute(unpatched, a.name, a.param_id, out, writeAutomation);
	}
}

void serializeModControllableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                       const int32_t* valuesForOverride) {
	writeDocParamGroup(out, "equalizer", kEqualizerGroup, unpatched, writeAutomation, valuesForOverride);
}

namespace {
// containsSomething(neutral): automated, or currentValue != neutral (unsigned compare) — the engine's
// AutoParam::containsSomething. Used for GlobalEffectable's bespoke conditional attrs.
bool docContainsSomething(const DocAutoParam& param, uint32_t neutral) {
	return !param.nodes.empty() || static_cast<uint32_t>(param.current_value) != neutral;
}
// The GlobalEffectable <delay> / <lpf> / <hpf> nested groups (global_effectable.cpp), in file order.
constexpr DocParamAttr kGlobalDelayGroup[] = {
    {"rate", params::UNPATCHED_DELAY_RATE},
    {"feedback", params::UNPATCHED_DELAY_AMOUNT},
};
constexpr DocParamAttr kGlobalLpfGroup[] = {
    {"frequency", params::UNPATCHED_LPF_FREQ},
    {"resonance", params::UNPATCHED_LPF_RES},
};
constexpr DocParamAttr kGlobalHpfGroup[] = {
    {"frequency", params::UNPATCHED_HPF_FREQ},
    {"resonance", params::UNPATCHED_HPF_RES},
};
} // namespace

void serializeGlobalEffectableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                              const int32_t* ovr) {
	// Byte-for-byte reproduction of GlobalEffectable::writeParamAttributesToFile. The flat global attrs
	// wrap the shared ModControllableAudio attrs; pitchAdjust and sidechainCompressorVolume are written
	// only when they contain something (with their own neutral values), the rest unconditionally.
	writeDocParamAsAttribute(unpatched, "reverbAmount", params::UNPATCHED_REVERB_SEND_AMOUNT, out, writeAutomation,
	                         false, ovr);
	writeDocParamAsAttribute(unpatched, "volume", params::UNPATCHED_VOLUME, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "pan", params::UNPATCHED_PAN, out, writeAutomation, false, ovr);

	if (docContainsSomething(unpatched.params[params::UNPATCHED_PITCH_ADJUST], 0)) {
		writeDocParamAsAttribute(unpatched, "pitchAdjust", params::UNPATCHED_PITCH_ADJUST, out, writeAutomation, false,
		                         ovr);
	}
	if (docContainsSomething(unpatched.params[params::UNPATCHED_SIDECHAIN_VOLUME], 0x80000000u)) {
		writeDocParamAsAttribute(unpatched, "sidechainCompressorVolume", params::UNPATCHED_SIDECHAIN_VOLUME, out,
		                         writeAutomation, false, ovr);
	}

	writeDocParamAsAttribute(unpatched, "sidechainCompressorShape", params::UNPATCHED_SIDECHAIN_SHAPE, out,
	                         writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "modFXDepth", params::UNPATCHED_MOD_FX_DEPTH, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "modFXRate", params::UNPATCHED_MOD_FX_RATE, out, writeAutomation, false, ovr);

	serializeModControllableParamAttributes(unpatched, out, writeAutomation, ovr);

	// Community Firmware parameters (always written after the official ones, just before the parent closes).
	writeDocParamAsAttribute(unpatched, "lpfMorph", params::UNPATCHED_LPF_MORPH, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "hpfMorph", params::UNPATCHED_HPF_MORPH, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "tempo", params::UNPATCHED_TEMPO, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "arpeggiatorRate", params::UNPATCHED_ARP_RATE, out, writeAutomation, false,
	                         ovr);
}

void serializeGlobalEffectableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                        const int32_t* ovr) {
	// Byte-for-byte reproduction of GlobalEffectable::writeParamTagsToFile: the <delay>/<lpf>/<hpf>
	// groups, then the shared ModControllableAudio tags (<equalizer>).
	writeDocParamGroup(out, "delay", kGlobalDelayGroup, unpatched, writeAutomation, ovr);
	writeDocParamGroup(out, "lpf", kGlobalLpfGroup, unpatched, writeAutomation, ovr);
	writeDocParamGroup(out, "hpf", kGlobalHpfGroup, unpatched, writeAutomation, ovr);
	serializeModControllableParamTags(unpatched, out, writeAutomation, ovr);
}

namespace {
// The ExpressionParamSet (MPE) dimensions, by index, matching expressionParamNames[] in param_set.cpp.
constexpr DocParamAttr kExpressionParams[] = {
    {"pitchBend", 0},
    {"yExpression", 1},
    {"pressure", 2},
};
} // namespace

bool serializeExpressionParams(const DocParamSet& expression, Serializer& out, bool mustWriteOpeningTagEndFirst,
                               bool writeAutomation) {
	// Byte-for-byte reproduction of ExpressionParamSet::writeToFile.
	bool writtenAnyYet = false;
	for (const DocParamAttr& a : kExpressionParams) {
		if (docContainsSomething(expression.params[a.param_id], 0)) {
			if (!writtenAnyYet) {
				writtenAnyYet = true;
				if (mustWriteOpeningTagEndFirst) {
					out.writeOpeningTagEnd();
				}
				out.writeOpeningTagBeginning("expressionData");
			}
			writeDocParamAsAttribute(expression, a.name, a.param_id, out, writeAutomation);
		}
	}
	if (writtenAnyYet) {
		out.closeTag();
	}
	return writtenAnyYet;
}

namespace {
// Sound's four <envelope*> nested groups (sound.cpp), each {attack, decay, sustain, release} from the
// patched collection.
constexpr DocParamAttr kEnvelope1[] = {
    {"attack", params::LOCAL_ENV_0_ATTACK},
    {"decay", params::LOCAL_ENV_0_DECAY},
    {"sustain", params::LOCAL_ENV_0_SUSTAIN},
    {"release", params::LOCAL_ENV_0_RELEASE},
};
constexpr DocParamAttr kEnvelope2[] = {
    {"attack", params::LOCAL_ENV_1_ATTACK},
    {"decay", params::LOCAL_ENV_1_DECAY},
    {"sustain", params::LOCAL_ENV_1_SUSTAIN},
    {"release", params::LOCAL_ENV_1_RELEASE},
};
constexpr DocParamAttr kEnvelope3[] = {
    {"attack", params::LOCAL_ENV_2_ATTACK},
    {"decay", params::LOCAL_ENV_2_DECAY},
    {"sustain", params::LOCAL_ENV_2_SUSTAIN},
    {"release", params::LOCAL_ENV_2_RELEASE},
};
constexpr DocParamAttr kEnvelope4[] = {
    {"attack", params::LOCAL_ENV_3_ATTACK},
    {"decay", params::LOCAL_ENV_3_DECAY},
    {"sustain", params::LOCAL_ENV_3_SUSTAIN},
    {"release", params::LOCAL_ENV_3_RELEASE},
};
} // namespace

void serializeSoundParams(const DocParamSet& patched, const DocParamSet& unpatched,
                          const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation) {
	// Byte-for-byte reproduction of Sound::writeParamsToFile. Two unpatched flat attrs lead, then the
	// patched flat attrs; the pitchAdjust family is onlyIfContainsSomething; the shared ModControllableAudio
	// attrs sit before the community attrs (lpfMorph/hpfMorph/waveFold).
	writeDocParamAsAttribute(unpatched, "portamento", params::UNPATCHED_PORTAMENTO, out, writeAutomation);
	writeDocParamAsAttribute(unpatched, "compressorShape", params::UNPATCHED_SIDECHAIN_SHAPE, out, writeAutomation);

	writeDocParamAsAttribute(patched, "oscAVolume", params::LOCAL_OSC_A_VOLUME, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscAPulseWidth", params::LOCAL_OSC_A_PHASE_WIDTH, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscAWavetablePosition", params::LOCAL_OSC_A_WAVE_INDEX, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscBVolume", params::LOCAL_OSC_B_VOLUME, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscBPulseWidth", params::LOCAL_OSC_B_PHASE_WIDTH, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscBWavetablePosition", params::LOCAL_OSC_B_WAVE_INDEX, out, writeAutomation);
	writeDocParamAsAttribute(patched, "noiseVolume", params::LOCAL_NOISE_VOLUME, out, writeAutomation);

	writeDocParamAsAttribute(patched, "volume", params::GLOBAL_VOLUME_POST_FX, out, writeAutomation);
	writeDocParamAsAttribute(patched, "pan", params::LOCAL_PAN, out, writeAutomation);

	writeDocParamAsAttribute(patched, "lpfFrequency", params::LOCAL_LPF_FREQ, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lpfResonance", params::LOCAL_LPF_RESONANCE, out, writeAutomation);

	writeDocParamAsAttribute(patched, "hpfFrequency", params::LOCAL_HPF_FREQ, out, writeAutomation);
	writeDocParamAsAttribute(patched, "hpfResonance", params::LOCAL_HPF_RESONANCE, out, writeAutomation);

	writeDocParamAsAttribute(patched, "lfo1Rate", params::GLOBAL_LFO_FREQ_1, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lfo2Rate", params::LOCAL_LFO_LOCAL_FREQ_1, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lfo3Rate", params::GLOBAL_LFO_FREQ_2, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lfo4Rate", params::LOCAL_LFO_LOCAL_FREQ_2, out, writeAutomation);

	writeDocParamAsAttribute(patched, "modulator1Amount", params::LOCAL_MODULATOR_0_VOLUME, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modulator1Feedback", params::LOCAL_MODULATOR_0_FEEDBACK, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modulator2Amount", params::LOCAL_MODULATOR_1_VOLUME, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modulator2Feedback", params::LOCAL_MODULATOR_1_FEEDBACK, out, writeAutomation);

	writeDocParamAsAttribute(patched, "carrier1Feedback", params::LOCAL_CARRIER_0_FEEDBACK, out, writeAutomation);
	writeDocParamAsAttribute(patched, "carrier2Feedback", params::LOCAL_CARRIER_1_FEEDBACK, out, writeAutomation);

	writeDocParamAsAttribute(patched, "pitchAdjust", params::LOCAL_PITCH_ADJUST, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "oscAPitchAdjust", params::LOCAL_OSC_A_PITCH_ADJUST, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "oscBPitchAdjust", params::LOCAL_OSC_B_PITCH_ADJUST, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "mod1PitchAdjust", params::LOCAL_MODULATOR_0_PITCH_ADJUST, out, writeAutomation,
	                         true);
	writeDocParamAsAttribute(patched, "mod2PitchAdjust", params::LOCAL_MODULATOR_1_PITCH_ADJUST, out, writeAutomation,
	                         true);

	writeDocParamAsAttribute(patched, "modFXRate", params::GLOBAL_MOD_FX_RATE, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modFXDepth", params::GLOBAL_MOD_FX_DEPTH, out, writeAutomation);

	writeDocParamAsAttribute(patched, "delayRate", params::GLOBAL_DELAY_RATE, out, writeAutomation);
	writeDocParamAsAttribute(patched, "delayFeedback", params::GLOBAL_DELAY_FEEDBACK, out, writeAutomation);

	writeDocParamAsAttribute(patched, "reverbAmount", params::GLOBAL_REVERB_AMOUNT, out, writeAutomation);

	writeDocParamAsAttribute(patched, "arpeggiatorRate", params::GLOBAL_ARP_RATE, out, writeAutomation);

	serializeModControllableParamAttributes(unpatched, out, writeAutomation);

	// Community Firmware parameters (always written after the official ones, just before the parent closes).
	writeDocParamAsAttribute(patched, "lpfMorph", params::LOCAL_LPF_MORPH, out, writeAutomation);
	writeDocParamAsAttribute(patched, "hpfMorph", params::LOCAL_HPF_MORPH, out, writeAutomation);
	writeDocParamAsAttribute(patched, "waveFold", params::LOCAL_FOLD, out, writeAutomation);

	out.writeOpeningTagEnd();

	writeDocParamGroup(out, "envelope1", kEnvelope1, patched, writeAutomation);
	writeDocParamGroup(out, "envelope2", kEnvelope2, patched, writeAutomation);
	writeDocParamGroup(out, "envelope3", kEnvelope3, patched, writeAutomation);
	writeDocParamGroup(out, "envelope4", kEnvelope4, patched, writeAutomation);

	serializePatchCables(cables, out, writeAutomation);

	serializeModControllableParamTags(unpatched, out, writeAutomation);
}

void serializePatchCables(const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation) {
	// Byte-for-byte reproduction of PatchCableSet::writePatchCablesToFile. The early-out matches the
	// engine's `if (!numPatchCables) return;`.
	if (cables.empty()) {
		return;
	}
	out.writeArrayStart("patchCables");
	for (const DocPatchCable& cable : cables) {
		// Depth-controlling (range) cables aren't written at top level; they're nested below the cable
		// whose depth they control.
		if (!cable.destination.isJustAParam()) {
			continue;
		}
		out.writeOpeningTagBeginning("patchCable", true);
		out.writeAttribute("source", sourceToString(cable.from));
		out.writeAttribute("destination", params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
		                                                           cable.destination.getJustTheParam()));
		out.writeAttribute("polarity", polarityToString(cable.polarity).data());
		out.insertCommaIfNeeded();
		out.write("\n");
		out.printIndents();
		out.writeTagNameAndSeperator("amount");
		out.write("\"");
		writeDocAutoParam(cable.amount, out, writeAutomation);
		out.write("\"");

		// Find the cable(s) whose destination is this cable's param-with-this-source — they control its
		// depth. NB: like the engine, the nested cable writes the OUTER cable's polarity (a quirk
		// preserved for byte-identity).
		ParamDescriptor controllerDescriptor = cable.destination;
		controllerDescriptor.addSource(cable.from);
		bool anyDepthControllingCablesFound = false;
		for (const DocPatchCable& d : cables) {
			if (d.destination == controllerDescriptor) {
				if (!anyDepthControllingCablesFound) {
					anyDepthControllingCablesFound = true;
					out.writeOpeningTagEnd();
					out.writeArrayStart("depthControlledBy");
				}
				out.writeOpeningTagBeginning("patchCable", true);
				out.writeAttribute("source", sourceToString(d.from));
				out.writeAttribute("polarity", polarityToString(cable.polarity).data());
				out.insertCommaIfNeeded();
				out.write("\n");
				out.printIndents();
				out.writeTagNameAndSeperator("amount");
				out.write("\"");
				writeDocAutoParam(d.amount, out, writeAutomation);
				out.write("\"");
				out.closeTag(true);
			}
		}
		if (anyDepthControllingCablesFound) {
			out.writeArrayEnding("depthControlledBy");
			out.writeClosingTag("patchCable", true, true);
		}
		else {
			out.closeTag(true);
		}
	}
	out.writeArrayEnding("patchCables");
}

DocAutoParam readDocAutoParam(Deserializer& in, int32_t readAutomationUpToPos) {
	// Reuse the engine's leaf parser (hex blob, node reading, back-compat) on a throwaway AutoParam,
	// then project the result — no reimplementation of the hex format.
	AutoParam tmp;
	tmp.readFromFile(in, readAutomationUpToPos);
	return toDocAutoParam(tmp);
}

void deserializePatchCables(std::vector<DocPatchCable>& out, Deserializer& in, int32_t readAutomationUpToPos) {
	// Mirrors the structure of PatchCableSet::readPatchCablesFromFile (current format). The pre-V3.2
	// quirks (rangeAdjustable / PLACEHOLDER_RANGE, the X→pitch-adjust special case, and the
	// duplicate-cable dedup) are intentionally dropped.
	namespace params = deluge::modulation::params;
	struct RangeCable {
		PatchSource from;
		Polarity polarity;
		DocAutoParam amount;
	};

	char const* tagName;
	in.match('[');
	while (in.match('{') && *(tagName = in.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "patchCable")) {
			in.match('{');
			PatchSource source = PatchSource::NONE;
			ParamDescriptor destination;
			destination.setToNull();
			DocAutoParam amount;
			Polarity polarity = Polarity::BIPOLAR;
			std::vector<RangeCable> rangeCables;

			while (*(tagName = in.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "source")) {
					source = stringToSource(in.readTagOrAttributeValue());
					if (source == PatchSource::AFTERTOUCH) {
						polarity = Polarity::UNIPOLAR;
					}
				}
				else if (!strcmp(tagName, "destination")) {
					destination.setToHaveParamOnly(
					    params::fileStringToParam(params::Kind::UNPATCHED_SOUND, in.readTagOrAttributeValue(), true));
				}
				else if (!strcmp(tagName, "amount")) {
					amount = readDocAutoParam(in, readAutomationUpToPos);
				}
				else if (!strcmp(tagName, "polarity")) {
					polarity = stringToPolarity(in.readTagOrAttributeValue());
				}
				else if (!strcmp(tagName, "depthControlledBy")) {
					in.match('[');
					while (in.match('{') && *(tagName = in.readNextTagOrAttributeName())) {
						if (!strcmp(tagName, "patchCable")) {
							in.match('{');
							PatchSource rangeSource = PatchSource::NONE;
							Polarity rangePolarity = Polarity::BIPOLAR;
							DocAutoParam rangeAmount;
							while (*(tagName = in.readNextTagOrAttributeName())) {
								if (!strcmp(tagName, "source")) {
									rangeSource = stringToSource(in.readTagOrAttributeValue());
								}
								else if (!strcmp(tagName, "amount")) {
									rangeAmount = readDocAutoParam(in, readAutomationUpToPos);
								}
								else if (!strcmp(tagName, "polarity")) {
									rangePolarity = stringToPolarity(in.readTagOrAttributeValue());
								}
								in.exitTag();
							}
							in.match('}');
							in.match('}');
							if (rangeSource != PatchSource::NONE) {
								rangeCables.push_back({rangeSource, rangePolarity, rangeAmount});
							}
						}
						in.exitTag();
					}
				}
				in.match(']'); // closes a depthControlledBy array; a no-op for the simple attributes
				in.exitTag();
			}

			if (source != PatchSource::NONE && !destination.isNull()) {
				// Range cables first (their destination is this cable's param + this source), then the
				// controlled cable — matching the engine's append order.
				for (const RangeCable& rc : rangeCables) {
					ParamDescriptor rangeDestination = destination;
					rangeDestination.addSource(source);
					out.push_back(DocPatchCable{.from = rc.from,
					                            .destination = rangeDestination,
					                            .polarity = rc.polarity,
					                            .amount = rc.amount});
				}
				out.push_back(
				    DocPatchCable{.from = source, .destination = destination, .polarity = polarity, .amount = amount});
			}
		}
		in.exitTag(nullptr, true);
		in.match('}');
	}
	in.match(']');
}

} // namespace deluge::project
