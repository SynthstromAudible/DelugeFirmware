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

#include "model/project_bridge/param_serialization.h"

#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/patch_cable_words.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"  // intToHex
#include "util/functions.h" // sourceToString
#include <algorithm>
#include <cstdint>

namespace deluge::bridge {

using deluge::engine::ParamId;
namespace ip = deluge::instrument::param;
namespace gp = deluge::global_effectable::param;

using namespace deluge::project;

namespace params = deluge::modulation::params;

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

void writeDocParamAsAttribute(const DocParamSet& doc, char const* name, ParamId id, Serializer& out,
                              bool writeAutomation, bool onlyIfContainsSomething, const int32_t* valuesForOverride) {
	// Byte-for-byte reproduction of ParamSet::writeParamAsAttribute.
	const size_t p = *doc.indexOf(id); // the override arrays are indexed by position in the set
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
    {"stutterRate", ip::kStutterRate},     {"sampleRateReduction", ip::kSampleRateReduction},
    {"bitCrush", ip::kBitcrushAmount},     {"modFXOffset", ip::kModFXOffset},
    {"modFXFeedback", ip::kModFXFeedback}, {"compressorThreshold", ip::kCompressorThreshold},
};
// The remaining ModControllableAudio attrs (arp / probability / spread) are written without override —
// the engine omits valuesForOverride for these.
constexpr DocParamAttr kModControllableArpAttrs[] = {
    {"arpeggiatorGate", ip::kArpGate},           {"noteProbability", ip::kNoteProbability},
    {"bassProbability", ip::kBassProbability},   {"swapProbability", ip::kSwapProbability},
    {"glideProbability", ip::kGlideProbability}, {"reverseProbability", ip::kReverseProbability},
    {"chordProbability", ip::kChordProbability}, {"ratchetProbability", ip::kRatchetProbability},
    {"ratchetAmount", ip::kRatchetAmount},       {"sequenceLength", ip::kSequenceLength},
    {"chordPolyphony", ip::kChordPolyphony},     {"rhythm", ip::kRhythm},
    {"spreadVelocity", ip::kSpreadVelocity},     {"spreadGate", ip::kSpreadGate},
    {"spreadOctave", ip::kSpreadOctave},
};
constexpr DocParamAttr kEqualizerGroup[] = {
    {"bass", ip::kBass},
    {"treble", ip::kTreble},
    {"bassFrequency", ip::kBassFreq},
    {"trebleFrequency", ip::kTrebleFreq},
};
} // namespace

void writeDocParamGroup(Serializer& out, const char* groupName, std::span<const DocParamAttr> params_,
                        const DocParamSet& doc, bool writeAutomation, const int32_t* valuesForOverride) {
	out.writeOpeningTagBeginning(groupName);
	for (const DocParamAttr& a : params_) {
		writeDocParamAsAttribute(doc, a.name, a.id, out, writeAutomation, false, valuesForOverride);
	}
	out.closeTag();
}

void serializeModControllableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                             const int32_t* valuesForOverride) {
	for (const DocParamAttr& a : kModControllableOverrideAttrs) {
		writeDocParamAsAttribute(unpatched, a.name, a.id, out, writeAutomation, false, valuesForOverride);
	}
	// The arp/probability/spread attrs are always written without the override (engine parity).
	for (const DocParamAttr& a : kModControllableArpAttrs) {
		writeDocParamAsAttribute(unpatched, a.name, a.id, out, writeAutomation);
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
    {"rate", gp::kDelayRate},
    {"feedback", gp::kDelayFeedback},
};
constexpr DocParamAttr kGlobalLpfGroup[] = {
    {"frequency", gp::kLpfFrequency},
    {"resonance", gp::kLpfResonance},
};
constexpr DocParamAttr kGlobalHpfGroup[] = {
    {"frequency", gp::kHpfFrequency},
    {"resonance", gp::kHpfResonance},
};
} // namespace

void serializeGlobalEffectableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                              const int32_t* ovr) {
	// Byte-for-byte reproduction of GlobalEffectable::writeParamAttributesToFile. The flat global attrs
	// wrap the shared ModControllableAudio attrs; pitchAdjust and sidechainCompressorVolume are written
	// only when they contain something (with their own neutral values), the rest unconditionally.
	writeDocParamAsAttribute(unpatched, "reverbAmount", gp::kReverbAmount, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "volume", gp::kVolume, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "pan", gp::kPan, out, writeAutomation, false, ovr);

	if (docContainsSomething(*unpatched.find(gp::kPitchAdjust), 0)) {
		writeDocParamAsAttribute(unpatched, "pitchAdjust", gp::kPitchAdjust, out, writeAutomation, false, ovr);
	}
	if (docContainsSomething(*unpatched.find(gp::kSidechainCompressorVolume), 0x80000000u)) {
		writeDocParamAsAttribute(unpatched, "sidechainCompressorVolume", gp::kSidechainCompressorVolume, out,
		                         writeAutomation, false, ovr);
	}

	writeDocParamAsAttribute(unpatched, "sidechainCompressorShape", ip::kCompressorShape, out, writeAutomation, false,
	                         ovr);
	writeDocParamAsAttribute(unpatched, "modFXDepth", gp::kModFXDepth, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "modFXRate", gp::kModFXRate, out, writeAutomation, false, ovr);

	serializeModControllableParamAttributes(unpatched, out, writeAutomation, ovr);

	// Community Firmware parameters (always written after the official ones, just before the parent closes).
	writeDocParamAsAttribute(unpatched, "lpfMorph", gp::kLpfMorph, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "hpfMorph", gp::kHpfMorph, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "tempo", gp::kTempo, out, writeAutomation, false, ovr);
	writeDocParamAsAttribute(unpatched, "arpeggiatorRate", gp::kArpRate, out, writeAutomation, false, ovr);
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
    {"pitchBend", ParamId{0}},
    {"yExpression", ParamId{1}},
    {"pressure", ParamId{2}},
};
} // namespace

bool serializeExpressionParams(const DocParamSet& expression, Serializer& out, bool mustWriteOpeningTagEndFirst,
                               bool writeAutomation) {
	// Byte-for-byte reproduction of ExpressionParamSet::writeToFile.
	bool writtenAnyYet = false;
	for (const DocParamAttr& a : kExpressionParams) {
		if (docContainsSomething(*expression.find(a.id), 0)) {
			if (!writtenAnyYet) {
				writtenAnyYet = true;
				if (mustWriteOpeningTagEndFirst) {
					out.writeOpeningTagEnd();
				}
				out.writeOpeningTagBeginning("expressionData");
			}
			writeDocParamAsAttribute(expression, a.name, a.id, out, writeAutomation);
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
    {"attack", ip::kEnv1Attack},
    {"decay", ip::kEnv1Decay},
    {"sustain", ip::kEnv1Sustain},
    {"release", ip::kEnv1Release},
};
constexpr DocParamAttr kEnvelope2[] = {
    {"attack", ip::kEnv2Attack},
    {"decay", ip::kEnv2Decay},
    {"sustain", ip::kEnv2Sustain},
    {"release", ip::kEnv2Release},
};
constexpr DocParamAttr kEnvelope3[] = {
    {"attack", ip::kEnv3Attack},
    {"decay", ip::kEnv3Decay},
    {"sustain", ip::kEnv3Sustain},
    {"release", ip::kEnv3Release},
};
constexpr DocParamAttr kEnvelope4[] = {
    {"attack", ip::kEnv4Attack},
    {"decay", ip::kEnv4Decay},
    {"sustain", ip::kEnv4Sustain},
    {"release", ip::kEnv4Release},
};
} // namespace

void serializeSoundParams(const DocParamSet& patched, const DocParamSet& unpatched,
                          const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation) {
	// Byte-for-byte reproduction of Sound::writeParamsToFile. Two unpatched flat attrs lead, then the
	// patched flat attrs; the pitchAdjust family is onlyIfContainsSomething; the shared ModControllableAudio
	// attrs sit before the community attrs (lpfMorph/hpfMorph/waveFold).
	writeDocParamAsAttribute(unpatched, "portamento", ip::kPortamento, out, writeAutomation);
	writeDocParamAsAttribute(unpatched, "compressorShape", ip::kCompressorShape, out, writeAutomation);

	writeDocParamAsAttribute(patched, "oscAVolume", ip::kOscAVolume, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscAPulseWidth", ip::kOscAPhaseWidth, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscAWavetablePosition", ip::kOscAWavetablePosition, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscBVolume", ip::kOscBVolume, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscBPulseWidth", ip::kOscBPhaseWidth, out, writeAutomation);
	writeDocParamAsAttribute(patched, "oscBWavetablePosition", ip::kOscBWavetablePosition, out, writeAutomation);
	writeDocParamAsAttribute(patched, "noiseVolume", ip::kNoiseVolume, out, writeAutomation);

	writeDocParamAsAttribute(patched, "volume", ip::kVolumePostFX, out, writeAutomation);
	writeDocParamAsAttribute(patched, "pan", ip::kPan, out, writeAutomation);

	writeDocParamAsAttribute(patched, "lpfFrequency", ip::kLpfFrequency, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lpfResonance", ip::kLpfResonance, out, writeAutomation);

	writeDocParamAsAttribute(patched, "hpfFrequency", ip::kHpfFrequency, out, writeAutomation);
	writeDocParamAsAttribute(patched, "hpfResonance", ip::kHpfResonance, out, writeAutomation);

	writeDocParamAsAttribute(patched, "lfo1Rate", ip::kLfo1Rate, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lfo2Rate", ip::kLfo2Rate, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lfo3Rate", ip::kLfo3Rate, out, writeAutomation);
	writeDocParamAsAttribute(patched, "lfo4Rate", ip::kLfo4Rate, out, writeAutomation);

	writeDocParamAsAttribute(patched, "modulator1Amount", ip::kModulator1Volume, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modulator1Feedback", ip::kModulator1Feedback, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modulator2Amount", ip::kModulator2Volume, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modulator2Feedback", ip::kModulator2Feedback, out, writeAutomation);

	writeDocParamAsAttribute(patched, "carrier1Feedback", ip::kCarrier1Feedback, out, writeAutomation);
	writeDocParamAsAttribute(patched, "carrier2Feedback", ip::kCarrier2Feedback, out, writeAutomation);

	writeDocParamAsAttribute(patched, "pitchAdjust", ip::kPitch, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "oscAPitchAdjust", ip::kOscAPitch, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "oscBPitchAdjust", ip::kOscBPitch, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "mod1PitchAdjust", ip::kModulator1Pitch, out, writeAutomation, true);
	writeDocParamAsAttribute(patched, "mod2PitchAdjust", ip::kModulator2Pitch, out, writeAutomation, true);

	writeDocParamAsAttribute(patched, "modFXRate", ip::kModFXRate, out, writeAutomation);
	writeDocParamAsAttribute(patched, "modFXDepth", ip::kModFXDepth, out, writeAutomation);

	writeDocParamAsAttribute(patched, "delayRate", ip::kDelayRate, out, writeAutomation);
	writeDocParamAsAttribute(patched, "delayFeedback", ip::kDelayFeedback, out, writeAutomation);

	writeDocParamAsAttribute(patched, "reverbAmount", ip::kReverbAmount, out, writeAutomation);

	writeDocParamAsAttribute(patched, "arpeggiatorRate", ip::kArpRate, out, writeAutomation);

	serializeModControllableParamAttributes(unpatched, out, writeAutomation);

	// Community Firmware parameters (always written after the official ones, just before the parent closes).
	writeDocParamAsAttribute(patched, "lpfMorph", ip::kLpfMorph, out, writeAutomation);
	writeDocParamAsAttribute(patched, "hpfMorph", ip::kHpfMorph, out, writeAutomation);
	writeDocParamAsAttribute(patched, "waveFold", ip::kWaveFold, out, writeAutomation);

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
		if (!destination_of(cable).isJustAParam()) {
			continue;
		}
		out.writeOpeningTagBeginning("patchCable", true);
		out.writeAttribute("source", sourceToString(source_of(cable)));
		out.writeAttribute("destination", params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
		                                                           destination_of(cable).getJustTheParam()));
		out.writeAttribute("polarity", polarityToString(polarity_of(cable)).data());
		out.insertCommaIfNeeded();
		out.write("\n");
		out.printIndents();
		out.writeTagNameAndSeperator("amount");
		out.write("\"");
		writeDocAutoParam(cable.amount, out, writeAutomation);
		out.write("\"");

		// Find the cable(s) whose destination is this cable's param-with-this-source — they control its
		// depth. Each writes its own polarity.
		ParamDescriptor controllerDescriptor = destination_of(cable);
		controllerDescriptor.addSource(source_of(cable));
		bool anyDepthControllingCablesFound = false;
		for (const DocPatchCable& d : cables) {
			if (destination_of(d) == controllerDescriptor) {
				if (!anyDepthControllingCablesFound) {
					anyDepthControllingCablesFound = true;
					out.writeOpeningTagEnd();
					out.writeArrayStart("depthControlledBy");
				}
				out.writeOpeningTagBeginning("patchCable", true);
				out.writeAttribute("source", sourceToString(source_of(d)));
				out.writeAttribute("polarity", polarityToString(polarity_of(d)).data());
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

void serializeMidiParams(const std::vector<DocMidiParam>& midiParams, Serializer& out, bool writeAutomation) {
	// Byte-for-byte reproduction of MIDIParamCollection::writeToFile. Early-out matches the engine's
	// `if (!params.empty())`; each param emits a <cc> tag (number, or "none" for CC_NUMBER_NONE) and a <value>
	// leaf. writeDocAutoParam writes exactly what AutoParam::writeToFile does (the "0x…" hex + automation nodes).
	if (midiParams.empty()) {
		return;
	}
	out.writeOpeningTag("midiParams");
	for (const DocMidiParam& mp : midiParams) {
		out.writeOpeningTag("param");
		int32_t cc = mp.cc;
		if (cc == CC_NUMBER_NONE) {
			out.writeTag("cc", "none");
		}
		else {
			out.writeTag("cc", cc);
		}
		// <value> is a text-content element (its hex is written incrementally): an element in XML,
		// a string in JSON (writeOpeningTag would make it an invalid object box {0x…} in JSON).
		out.writeOpeningTextTag("value");
		writeDocAutoParam(mp.param, out, writeAutomation);
		out.writeClosingTextTag("value");
		out.writeClosingTag("param");
	}
	out.writeClosingTag("midiParams");
}

DocAutoParam readDocAutoParam(Deserializer& in, int32_t readAutomationUpToPos) {
	// Reuse the engine's leaf parser (hex blob, node reading, back-compat) on a throwaway AutoParam, reading the
	// nodes straight into a local DocAutoParam (M2-F: no AutoParam inline storage). readFromFile sets tmp's RT
	// cursor; the nodes land in `result`.
	AutoParam tmp;
	DocAutoParam result;
	tmp.readFromFile(result, in, readAutomationUpToPos);
	result.current_value = tmp.currentValue;
	return result;
}

// --- DOM-native read of the owner param blocks (Phase 5b) ---

namespace {
// Read one leaf into a live param + mark its automation flag (matching ParamSet::readParam). The leaf
// goes through the DOM (readDocAutoParam → fromDocAutoParam) so the file format + back-compat are shared
// with the write side; the net effect on currentValue + nodes is identical to a direct readParam.
void readLiveParam(Deserializer& in, ParamSet& set, ParamCollectionSummary& summary, ParamId id,
                   int32_t readAutomationUpToPos) {
	// The live set holds the param at its runtime index, through its engine's map.
	const int32_t p = *runtimeIndexOf(set.getParamKind(), id);
	DocAutoParam doc = readDocAutoParam(in, readAutomationUpToPos);
	// M2-F: land the value in the RT cursor and the nodes in the set's owner-resolved DOM store (not ownDoc_).
	fromDocAutoParam(doc, set.params[p].currentValue, set.docForParam(p)->nodes);
	if (set.params[p].isAutomated(*set.docForParam(p))) {
		set.paramHasAutomationNow(&summary, p);
	}
}

// If tagName matches a flat attr in the table, read it into the live set and return true.
bool readLiveParamFromTable(Deserializer& in, char const* tagName, std::span<const DocParamAttr> table, ParamSet& set,
                            ParamCollectionSummary& summary, int32_t readAutomationUpToPos) {
	for (const DocParamAttr& a : table) {
		if (!strcmp(tagName, a.name)) {
			readLiveParam(in, set, summary, a.id, readAutomationUpToPos);
			in.exitTag(a.name);
			return true;
		}
	}
	return false;
}

// If tagName is the nested group tag, read its member attrs (current-format) into the live set and
// return true. Members the table doesn't know (legacy-only, e.g. morph in <lpf>) are skipped here and
// must be handled by the caller's legacy tail before calling this — so this is only invoked for groups
// whose current members are exhaustive, or the caller pre-handles the legacy members.
bool readLiveParamGroup(Deserializer& in, char const* tagName, const char* groupName,
                        std::span<const DocParamAttr> members, ParamSet& set, ParamCollectionSummary& summary,
                        int32_t readAutomationUpToPos) {
	if (strcmp(tagName, groupName) != 0) {
		return false;
	}
	in.match('{');
	while (*(tagName = in.readNextTagOrAttributeName())) {
		if (!readLiveParamFromTable(in, tagName, members, set, summary, readAutomationUpToPos)) {
			in.exitTag(tagName);
		}
	}
	in.exitTag(groupName, true);
	return true;
}
} // namespace

bool readModControllableParamTag(Deserializer& in, char const* tagName, ParamSet& unpatched,
                                 ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos) {
	return readLiveParamGroup(in, tagName, "equalizer", kEqualizerGroup, unpatched, unpatchedSummary,
	                          readAutomationUpToPos)
	       || readLiveParamFromTable(in, tagName, kModControllableOverrideAttrs, unpatched, unpatchedSummary,
	                                 readAutomationUpToPos)
	       || readLiveParamFromTable(in, tagName, kModControllableArpAttrs, unpatched, unpatchedSummary,
	                                 readAutomationUpToPos);
}

namespace {
// All GlobalEffectable flat attr names (read lookup — order-independent). The writer emits some of these
// conditionally (pitchAdjust / sidechainCompressorVolume), but on read every present tag is read.
constexpr DocParamAttr kGlobalEffectableFlatAttrs[] = {
    {"reverbAmount", gp::kReverbAmount},
    {"volume", gp::kVolume},
    {"pan", gp::kPan},
    {"pitchAdjust", gp::kPitchAdjust},
    {"sidechainCompressorVolume", gp::kSidechainCompressorVolume},
    {"sidechainCompressorShape", ip::kCompressorShape},
    {"modFXDepth", gp::kModFXDepth},
    {"modFXRate", gp::kModFXRate},
    {"lpfMorph", gp::kLpfMorph},
    {"hpfMorph", gp::kHpfMorph},
    {"tempo", gp::kTempo},
    {"arpeggiatorRate", gp::kArpRate},
};
// Read-side <lpf>/<hpf> groups: same as the write groups plus the legacy nested `morph` member.
constexpr DocParamAttr kGlobalLpfReadGroup[] = {
    {"frequency", gp::kLpfFrequency},
    {"resonance", gp::kLpfResonance},
    {"morph", gp::kLpfMorph},
};
constexpr DocParamAttr kGlobalHpfReadGroup[] = {
    {"frequency", gp::kHpfFrequency},
    {"resonance", gp::kHpfResonance},
    {"morph", gp::kHpfMorph},
};
} // namespace

bool readGlobalEffectableParamTag(Deserializer& in, char const* tagName, ParamSet& unpatched,
                                  ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos) {
	return readLiveParamGroup(in, tagName, "delay", kGlobalDelayGroup, unpatched, unpatchedSummary,
	                          readAutomationUpToPos)
	       || readLiveParamGroup(in, tagName, "lpf", kGlobalLpfReadGroup, unpatched, unpatchedSummary,
	                             readAutomationUpToPos)
	       || readLiveParamGroup(in, tagName, "hpf", kGlobalHpfReadGroup, unpatched, unpatchedSummary,
	                             readAutomationUpToPos)
	       || readLiveParamFromTable(in, tagName, kGlobalEffectableFlatAttrs, unpatched, unpatchedSummary,
	                                 readAutomationUpToPos)
	       || readModControllableParamTag(in, tagName, unpatched, unpatchedSummary, readAutomationUpToPos);
}

namespace {
// DOM-target leaf/table/group helpers — mirror readLiveParam* but land the DocAutoParam in the store's lane for its
// id (the persistent DOM store) instead of a live engine param. `doc.params` must be pre-sized.
void readDocParamLeaf(Deserializer& in, DocParamSet& doc, ParamId id, int32_t readAutomationUpToPos) {
	*doc.find(id) = readDocAutoParam(in, readAutomationUpToPos);
}

bool readDocParamFromTable(Deserializer& in, char const* tagName, std::span<const DocParamAttr> table, DocParamSet& doc,
                           int32_t readAutomationUpToPos) {
	for (const DocParamAttr& a : table) {
		if (!strcmp(tagName, a.name)) {
			readDocParamLeaf(in, doc, a.id, readAutomationUpToPos);
			in.exitTag(a.name);
			return true;
		}
	}
	return false;
}

bool readDocParamGroup(Deserializer& in, char const* tagName, const char* groupName,
                       std::span<const DocParamAttr> members, DocParamSet& doc, int32_t readAutomationUpToPos) {
	if (strcmp(tagName, groupName) != 0) {
		return false;
	}
	in.match('{');
	while (*(tagName = in.readNextTagOrAttributeName())) {
		if (!readDocParamFromTable(in, tagName, members, doc, readAutomationUpToPos)) {
			in.exitTag(tagName);
		}
	}
	in.exitTag(groupName, true);
	return true;
}
} // namespace

bool readModControllableParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                      int32_t readAutomationUpToPos) {
	return readDocParamGroup(in, tagName, "equalizer", kEqualizerGroup, doc, readAutomationUpToPos)
	       || readDocParamFromTable(in, tagName, kModControllableOverrideAttrs, doc, readAutomationUpToPos)
	       || readDocParamFromTable(in, tagName, kModControllableArpAttrs, doc, readAutomationUpToPos);
}

bool readGlobalEffectableParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                       int32_t readAutomationUpToPos) {
	return readDocParamGroup(in, tagName, "delay", kGlobalDelayGroup, doc, readAutomationUpToPos)
	       || readDocParamGroup(in, tagName, "lpf", kGlobalLpfReadGroup, doc, readAutomationUpToPos)
	       || readDocParamGroup(in, tagName, "hpf", kGlobalHpfReadGroup, doc, readAutomationUpToPos)
	       || readDocParamFromTable(in, tagName, kGlobalEffectableFlatAttrs, doc, readAutomationUpToPos)
	       || readModControllableParamTagToDoc(in, tagName, doc, readAutomationUpToPos);
}

namespace {
// Sound's two unpatched flat attrs (read lookup).
constexpr DocParamAttr kSoundUnpatchedAttrs[] = {
    {"portamento", ip::kPortamento},
    {"compressorShape", ip::kCompressorShape},
};
// Sound's patched flat attrs (read lookup; order-independent), matching Sound::writeParamsToFile.
constexpr DocParamAttr kSoundPatchedAttrs[] = {
    {"oscAVolume", ip::kOscAVolume},
    {"oscAPulseWidth", ip::kOscAPhaseWidth},
    {"oscAWavetablePosition", ip::kOscAWavetablePosition},
    {"oscBVolume", ip::kOscBVolume},
    {"oscBPulseWidth", ip::kOscBPhaseWidth},
    {"oscBWavetablePosition", ip::kOscBWavetablePosition},
    {"noiseVolume", ip::kNoiseVolume},
    {"volume", ip::kVolumePostFX},
    {"pan", ip::kPan},
    {"lpfFrequency", ip::kLpfFrequency},
    {"lpfResonance", ip::kLpfResonance},
    {"hpfFrequency", ip::kHpfFrequency},
    {"hpfResonance", ip::kHpfResonance},
    {"lfo1Rate", ip::kLfo1Rate},
    {"lfo2Rate", ip::kLfo2Rate},
    {"lfo3Rate", ip::kLfo3Rate},
    {"lfo4Rate", ip::kLfo4Rate},
    {"modulator1Amount", ip::kModulator1Volume},
    {"modulator1Feedback", ip::kModulator1Feedback},
    {"modulator2Amount", ip::kModulator2Volume},
    {"modulator2Feedback", ip::kModulator2Feedback},
    {"carrier1Feedback", ip::kCarrier1Feedback},
    {"carrier2Feedback", ip::kCarrier2Feedback},
    {"pitchAdjust", ip::kPitch},
    {"oscAPitchAdjust", ip::kOscAPitch},
    {"oscBPitchAdjust", ip::kOscBPitch},
    {"mod1PitchAdjust", ip::kModulator1Pitch},
    {"mod2PitchAdjust", ip::kModulator2Pitch},
    {"modFXRate", ip::kModFXRate},
    {"modFXDepth", ip::kModFXDepth},
    {"delayRate", ip::kDelayRate},
    {"delayFeedback", ip::kDelayFeedback},
    {"reverbAmount", ip::kReverbAmount},
    {"arpeggiatorRate", ip::kArpRate},
    {"lpfMorph", ip::kLpfMorph},
    {"hpfMorph", ip::kHpfMorph},
    {"waveFold", ip::kWaveFold},
};
} // namespace

bool readSoundParamTag(Deserializer& in, char const* tagName, ParamSet& patched, ParamCollectionSummary& patchedSummary,
                       ParamSet& unpatched, ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos) {
	return readLiveParamFromTable(in, tagName, kSoundUnpatchedAttrs, unpatched, unpatchedSummary, readAutomationUpToPos)
	       || readLiveParamFromTable(in, tagName, kSoundPatchedAttrs, patched, patchedSummary, readAutomationUpToPos)
	       || readLiveParamGroup(in, tagName, "envelope1", kEnvelope1, patched, patchedSummary, readAutomationUpToPos)
	       || readLiveParamGroup(in, tagName, "envelope2", kEnvelope2, patched, patchedSummary, readAutomationUpToPos)
	       || readLiveParamGroup(in, tagName, "envelope3", kEnvelope3, patched, patchedSummary, readAutomationUpToPos)
	       || readLiveParamGroup(in, tagName, "envelope4", kEnvelope4, patched, patchedSummary, readAutomationUpToPos)
	       || readModControllableParamTag(in, tagName, unpatched, unpatchedSummary, readAutomationUpToPos);
}

bool readSoundParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& patched, DocParamSet& unpatched,
                            int32_t readAutomationUpToPos) {
	return readDocParamFromTable(in, tagName, kSoundUnpatchedAttrs, unpatched, readAutomationUpToPos)
	       || readDocParamFromTable(in, tagName, kSoundPatchedAttrs, patched, readAutomationUpToPos)
	       || readDocParamGroup(in, tagName, "envelope1", kEnvelope1, patched, readAutomationUpToPos)
	       || readDocParamGroup(in, tagName, "envelope2", kEnvelope2, patched, readAutomationUpToPos)
	       || readDocParamGroup(in, tagName, "envelope3", kEnvelope3, patched, readAutomationUpToPos)
	       || readDocParamGroup(in, tagName, "envelope4", kEnvelope4, patched, readAutomationUpToPos)
	       || readModControllableParamTagToDoc(in, tagName, unpatched, readAutomationUpToPos);
}

bool readExpressionParamTag(Deserializer& in, char const* tagName, ParamSet& expression,
                            ParamCollectionSummary& summary, int32_t readAutomationUpToPos) {
	// kExpressionParams maps pitchBend/yExpression/pressure to dimensions 0/1/2 (shared with the writer).
	return readLiveParamFromTable(in, tagName, kExpressionParams, expression, summary, readAutomationUpToPos);
}

bool readExpressionParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                 int32_t readAutomationUpToPos) {
	if (readDocParamFromTable(in, tagName, kExpressionParams, doc, readAutomationUpToPos)) {
		return true;
	}
	// Legacy alias: early-alpha files stored pressure (dimension 2) as <channelPressure>.
	if (!strcmp(tagName, "channelPressure")) {
		readDocParamLeaf(in, doc, ParamId{2}, readAutomationUpToPos);
		in.exitTag(tagName);
		return true;
	}
	return false;
}

} // namespace deluge::bridge
