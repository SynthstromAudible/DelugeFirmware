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

#pragma once

#include "modulation/params/param.h" // deluge::modulation::params::Kind
#include "modulation/params/param_node.h"
#include "modulation/params/param_node_vector.h"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class AutoParam;
class ParamSet;
class ParamManager;
class Serializer;
class Deserializer;
enum class Polarity : uint8_t; // opaque-enum decl (fixed underlying type ⇒ usable by value); from patch_cable.h

namespace deluge::project {

/// Value-typed document form of one `AutoParam` — the automation leaf of the param/automation DOM
/// (docs/dev/param_automation_dom.md). `ParamNode` is already a `{pos, value, interpolated}` POD and is
/// reused verbatim; an empty `nodes` means the parameter is unautomated and `current_value` applies.
/// Patch-cable AutoParams store their node values in the patch-cable range — the projection copies the
/// stored representation faithfully (no reinterpretation).
struct DocAutoParam {
	int32_t current_value{0};
	std::vector<ParamNode> nodes;
};

/// The projection core operates on the raw pieces (an AutoParam's `currentValue` + its node vector) so
/// it is testable without the whole-app-coupled `AutoParam`; the `AutoParam` overloads below are thin
/// wrappers over these.
inline DocAutoParam toDocAutoParam(int32_t current_value, const ParamNodeVector& nodes) {
	DocAutoParam doc;
	doc.current_value = current_value;
	doc.nodes.reserve(nodes.size());
	for (const ParamNode& node : nodes) {
		doc.nodes.push_back(node);
	}
	return doc;
}

inline void fromDocAutoParam(const DocAutoParam& doc, int32_t& current_value, ParamNodeVector& nodes) {
	current_value = doc.current_value;
	nodes.clear();
	(void)nodes.reserveExtra(static_cast<int32_t>(doc.nodes.size()));
	for (const ParamNode& node : doc.nodes) {
		// doc.nodes are ascending by pos (copied from an ordered source), so each is the new last node.
		int32_t i = nodes.insertSorted(node.pos, /*isDefinitelyLast=*/true).value_or(-1);
		if (i < 0) {
			break; // allocation failed — leave what we have, matching AutoParam::readFromFile on OOM.
		}
		nodes[i] = node;
	}
}

/// Project to/from a live `AutoParam` (defined in param_automation.cpp; reads/writes its public
/// `currentValue` + `nodes` via the core above).
DocAutoParam toDocAutoParam(const AutoParam& param);
void fromDocAutoParam(const DocAutoParam& doc, AutoParam& param);

/// Value-typed document form of one `ParamSet` — a parameter collection (patched / unpatched /
/// expression). `params` is dense, indexed by param id within the collection (`params[p]` is the doc
/// form of the live `ParamSet::params[p]`), so its size is the collection's parameter count.
struct DocParamSet {
	deluge::modulation::params::Kind kind{deluge::modulation::params::Kind::NONE};
	std::vector<DocAutoParam> params;
};

/// Projection. The `(kind, params, numParams)` core operates on the raw AutoParam array so it is
/// testable from a plain array without constructing a live ParamSet (which needs a collection summary);
/// the `ParamSet&` wrappers apply it to a live collection via `params`/`getNumParams()`. All defined in
/// param_automation.cpp. `fromDocParamSet` writes at most `min(doc.params.size(), numParams)` params.
DocParamSet toDocParamSet(deluge::modulation::params::Kind kind, const AutoParam* params, int32_t numParams);
void fromDocParamSet(const DocParamSet& doc, AutoParam* params, int32_t numParams);
DocParamSet toDocParamSet(ParamSet& paramSet);
void fromDocParamSet(const DocParamSet& doc, ParamSet& paramSet);

/// Value-typed document form of one patch cable: a mod-matrix connection from a source to a destination
/// parameter, with an automatable amount. `destination` is the `ParamDescriptor` copied verbatim — it
/// encodes the target param and (for range cables) the cable-controlling-cable nesting, which the
/// projection need not interpret. The amount is stored in the patch-cable range [-2^30, 2^30).
struct DocPatchCable {
	PatchSource from{};
	ParamDescriptor destination{};
	Polarity polarity{};
	DocAutoParam amount;
};

/// Value-typed document form of a `ParamManager`'s collections. Each ParamSet is present only when the
/// live manager holds it (a sound has patched + unpatched + patch cables; a global-effectable / audio
/// clip has only unpatched; expression/MPE is optional). The MIDI-CC collection is projected in a later
/// sub-phase, so it is not yet a field here.
struct DocParamManager {
	std::optional<DocParamSet> unpatched;
	std::optional<DocParamSet> patched;
	std::optional<DocParamSet> expression;
	std::vector<DocPatchCable> patch_cables;
};

/// Project to/from a live `ParamManager` (defined in param_automation.cpp). `fromDocParamManager`
/// projects into an already-set-up manager — it fills the collections that exist in both the document
/// and the target (the engine builds the collection structure on load before the values are projected
/// in); it does not allocate or remove collections.
DocParamManager toDocParamManager(ParamManager& paramManager);
void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager);

// --- Generic serialization of the document model (param/automation DOM Phase 4) ---
// Emits the legacy song file format from the value DOM, replacing the engine's scattered
// self-serialization. This is the first contained piece (the <patchCables> block); it reproduces
// PatchCableSet::writePatchCablesToFile byte-for-byte and is verified against it, but is not yet wired
// into the song save path.

/// Write one AutoParam's value (+ automation when `writeAutomation`) as the legacy `0x…` hex blob — the
/// document-side equivalent of `AutoParam::writeToFile` (no override value). The caller emits the
/// surrounding tag/quotes.
void writeDocAutoParam(const DocAutoParam& doc, Serializer& out, bool writeAutomation);

/// Write one of a collection's params as a named attribute (`name="0x…"`), byte-identical to
/// `ParamSet::writeParamAsAttribute`. This is the building block the owner param blocks
/// (GlobalEffectable / ModControllableAudio / Sound / clips) are composed of. The value-override path
/// of the engine version is not yet supported.
void writeDocParamAsAttribute(const DocParamSet& doc, char const* name, int32_t p, Serializer& out,
                              bool writeAutomation, bool onlyIfContainsSomething = false);

/// One entry of an owner's param block: a file tag/attribute name and the param id it maps to within
/// the collection. Owner blocks are expressed as compact arrays of these driving the generic helpers.
struct DocParamAttr {
	const char* name;
	int32_t param_id;
};

/// Write a nested group of params (e.g. `<equalizer>` / `<lpf>`) from a collection — byte-identical to
/// the engine's inline `writeOpeningTagBeginning(group) + writeParamAsAttribute(tag, id)… + closeTag()`.
void writeDocParamGroup(Serializer& out, const char* groupName, std::span<const DocParamAttr> params,
                        const DocParamSet& doc, bool writeAutomation);

/// Serialize the ModControllableAudio shared param block from the unpatched collection, byte-identical
/// to `ModControllableAudio::writeParamAttributesToFile` (flat attrs) and `writeParamTagsToFile`
/// (the `<equalizer>` group). The two halves are emitted at different points in the owner's tag, so they
/// are separate (the value-override path is not yet supported).
void serializeModControllableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation);
void serializeModControllableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation);

/// Serialize the GlobalEffectable param block from the unpatched collection, byte-identical to
/// `GlobalEffectable::writeParamAttributesToFile` (its flat global attrs — reverb/volume/pan, the
/// conditional pitchAdjust + sidechain volume, modFX… — wrapped around the shared ModControllableAudio
/// attrs, then lpfMorph/hpfMorph/tempo/arpeggiatorRate) and `writeParamTagsToFile` (the `<delay>` /
/// `<lpf>` / `<hpf>` groups, then the shared ModControllableAudio tags). The value-override path is not
/// yet supported.
void serializeGlobalEffectableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation);
void serializeGlobalEffectableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation);

/// Serialize the ExpressionParamSet (MPE) block from the expression collection, byte-identical to
/// `ExpressionParamSet::writeToFile`: the `<expressionData>` group holding pitchBend / yExpression /
/// pressure, emitted only when at least one dimension contains something (and each dimension only when
/// it does). `mustWriteOpeningTagEndFirst` mirrors the engine arg — close the parent's opening tag
/// before the first child. Returns whether anything was written.
bool serializeExpressionParams(const DocParamSet& expression, Serializer& out, bool mustWriteOpeningTagEndFirst,
                               bool writeAutomation);

/// Serialize the Sound param block from a sound's collections, byte-identical to
/// `Sound::writeParamsToFile`: the flat patched + unpatched attrs (osc / filter / LFO / FM / envelope-less
/// flat params, the conditional pitchAdjust family) wrapping the shared ModControllableAudio attrs, then —
/// after the parent's opening tag is closed — the four `<envelope*>` groups, the `<patchCables>` block, and
/// the shared ModControllableAudio tags. Reads both `patched` and `unpatched` collections plus the patch
/// cables; emits `writeOpeningTagEnd` between the attrs and the child tags exactly where the engine does.
void serializeSoundParams(const DocParamSet& patched, const DocParamSet& unpatched,
                          const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation);

/// Write the `<patchCables>` block from the document's patch cables, byte-identical to
/// `PatchCableSet::writePatchCablesToFile` (including the depth-controlling / range-cable nesting).
void serializePatchCables(const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation);

/// Read one AutoParam from the legacy hex blob into the document form — the read counterpart of
/// `writeDocAutoParam`. Reuses the engine's `AutoParam::readFromFile` (so the leaf hex parsing and its
/// back-compat are not reimplemented), then projects the result.
DocAutoParam readDocAutoParam(Deserializer& in, int32_t readAutomationUpToPos);

/// Read a `<patchCables>` block (current format) into the document's patch cables — the inverse of
/// `serializePatchCables`. Parses the structure directly (resolving range/depth-controlling cables back
/// to flat cables) and reads amounts via `readDocAutoParam`. Pre-V3.2 patch-cable quirks
/// (`rangeAdjustable` / placeholder-range resolution) are intentionally not carried over.
void deserializePatchCables(std::vector<DocPatchCable>& out, Deserializer& in, int32_t readAutomationUpToPos);

} // namespace deluge::project
