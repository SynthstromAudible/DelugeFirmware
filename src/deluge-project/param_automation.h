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
class ParamCollectionSummary;
class PatchCableSet;
class MIDIParamCollection;
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
	ParamNodeVector nodes; // OrderedPosVector<ParamNode>: the single runtime store the engine borrows (Phase B)
};

/// The projection core operates on the raw pieces (an AutoParam's `currentValue` + its node vector) so
/// it is testable without the whole-app-coupled `AutoParam`; the `AutoParam` overloads below are thin
/// wrappers over these.
inline DocAutoParam toDocAutoParam(int32_t current_value, const ParamNodeVector& nodes) {
	DocAutoParam doc;
	doc.current_value = current_value;
	(void)doc.nodes.reserveExtra(static_cast<int32_t>(nodes.size()));
	for (const ParamNode& node : nodes) {
		// nodes are ascending by pos (copied from an ordered source), so each is the new last node.
		int32_t i = doc.nodes.insertSorted(node.pos, /*isDefinitelyLast=*/true).value_or(-1);
		if (i < 0) {
			break; // allocation failed — leave what we have.
		}
		doc.nodes[i] = node;
	}
	return doc;
}

inline void fromDocAutoParam(const DocAutoParam& doc, int32_t& current_value, ParamNodeVector& nodes) {
	current_value = doc.current_value;
	if (&nodes == &doc.nodes) {
		// Borrowed (Phase B): the engine's node store IS doc.nodes, so a copy would clear-then-read the same
		// vector. The value is set above; the nodes are already current by construction — nothing to copy.
		return;
	}
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

/// Value-typed document form of one `ParamSet` — a parameter collection (patched / unpatched /
/// expression). `params` is dense, indexed by param id within the collection (`params[p]` is the doc
/// form of the live `ParamSet::params[p]`), so its size is the collection's parameter count.
struct DocParamSet {
	deluge::modulation::params::Kind kind{deluge::modulation::params::Kind::NONE};
	std::vector<DocAutoParam> params;
};

/// Projection between a live ParamSet and its DocParamSet, owner-resolving each param's node store. Defined in
/// param_automation.cpp.
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

/// Value-typed document form of one MIDI-CC automation lane: the CC number that identifies it (the ordered
/// MIDIParamVector keys by cc) plus its automatable value. Keyed by identity (cc), like patch cables by
/// (from,dest) — the CC vector reorders on every learn, so index is not stable.
struct DocMidiParam {
	uint8_t cc{};
	DocAutoParam param;
};

/// Value-typed document form of a `ParamManager`'s collections. Each ParamSet is present only when the
/// live manager holds it (a sound has patched + unpatched + patch cables; a global-effectable / audio
/// clip has only unpatched; expression/MPE is optional). `midi_params` is populated only for MIDI_OUT
/// clip managers (a per-clip CC-automation store — MIDI has no instrument-shared default).
struct DocParamManager {
	std::optional<DocParamSet> unpatched;
	std::optional<DocParamSet> patched;
	std::optional<DocParamSet> expression;
	std::vector<DocPatchCable> patch_cables;
	std::vector<DocMidiParam> midi_params;
};

/// Project to/from a live `ParamManager` (defined in param_automation.cpp). `fromDocParamManager`
/// projects into an already-set-up manager — it fills the collections that exist in both the document
/// and the target (the engine builds the collection structure on load before the values are projected
/// in); it does not allocate or remove collections.
DocParamManager toDocParamManager(ParamManager& paramManager);
void fromDocParamManager(const DocParamManager& doc, ParamManager& paramManager);

/// Project a live PatchCableSet's cables into their document form — the identity-keyed persistent cable store the
/// engine set binds to (S4b). Each cable's saved fields are copied verbatim (the ParamDescriptor encodes the
/// destination + any range-cable nesting); nodes come from the cable's current store. Defined in
/// param_automation.cpp.
std::vector<DocPatchCable> toDocPatchCables(PatchCableSet& patchCableSet);

/// Project a live MIDIParamCollection's CC params into their document form — the identity-keyed (by cc) persistent
/// store the engine collection binds to (S4c). Nodes come from each CC's current store. Defined in
/// param_automation.cpp.
std::vector<DocMidiParam> toDocMidiParams(MIDIParamCollection& midiParamCollection);

/// Commit a non-automated unpatched param value through the document store (authoritative flip, Phase 5c):
/// record `value` into the DOM and return the value to project into the live AutoParam (so the live value
/// is DOM-derived, not set directly). Returns `value` unchanged if `doc` has no unpatched set or `p` is out
/// of range (legacy passthrough).

// --- Generic serialization of the document model (param/automation DOM Phase 4) ---
// Emits the legacy song file format from the value DOM, replacing the engine's scattered
// self-serialization. This is the first contained piece (the <patchCables> block); it reproduces
// PatchCableSet::writePatchCablesToFile byte-for-byte and is verified against it, but is not yet wired
// into the song save path.

/// Write one AutoParam's value (+ automation when `writeAutomation`) as the legacy `0x…` hex blob — the
/// document-side equivalent of `AutoParam::writeToFile`. The caller emits the surrounding tag/quotes.
/// `valueForOverride` mirrors the engine arg: when non-null and the param is automated, that value is
/// written in place of `current_value` (the Song-level "params in automation mode" override); the
/// automation nodes are always written as-is.
void writeDocAutoParam(const DocAutoParam& doc, Serializer& out, bool writeAutomation,
                       const int32_t* valueForOverride = nullptr);

/// Write one of a collection's params as a named attribute (`name="0x…"`), byte-identical to
/// `ParamSet::writeParamAsAttribute`. This is the building block the owner param blocks
/// (GlobalEffectable / ModControllableAudio / Sound / clips) are composed of. When `valuesForOverride` is
/// non-null its `[p]` entry is the override value (see `writeDocAutoParam`).
void writeDocParamAsAttribute(const DocParamSet& doc, char const* name, int32_t p, Serializer& out,
                              bool writeAutomation, bool onlyIfContainsSomething = false,
                              const int32_t* valuesForOverride = nullptr);

/// One entry of an owner's param block: a file tag/attribute name and the param id it maps to within
/// the collection. Owner blocks are expressed as compact arrays of these driving the generic helpers.
struct DocParamAttr {
	const char* name;
	int32_t param_id;
};

/// Write a nested group of params (e.g. `<equalizer>` / `<lpf>`) from a collection — byte-identical to
/// the engine's inline `writeOpeningTagBeginning(group) + writeParamAsAttribute(tag, id)… + closeTag()`.
void writeDocParamGroup(Serializer& out, const char* groupName, std::span<const DocParamAttr> params,
                        const DocParamSet& doc, bool writeAutomation, const int32_t* valuesForOverride = nullptr);

/// Serialize the ModControllableAudio shared param block from the unpatched collection, byte-identical
/// to `ModControllableAudio::writeParamAttributesToFile` (flat attrs) and `writeParamTagsToFile`
/// (the `<equalizer>` group). The two halves are emitted at different points in the owner's tag, so they
/// are separate (the value-override path is not yet supported).
void serializeModControllableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                             const int32_t* valuesForOverride = nullptr);
void serializeModControllableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                       const int32_t* valuesForOverride = nullptr);

/// Serialize the GlobalEffectable param block from the unpatched collection, byte-identical to
/// `GlobalEffectable::writeParamAttributesToFile` (its flat global attrs — reverb/volume/pan, the
/// conditional pitchAdjust + sidechain volume, modFX… — wrapped around the shared ModControllableAudio
/// attrs, then lpfMorph/hpfMorph/tempo/arpeggiatorRate) and `writeParamTagsToFile` (the `<delay>` /
/// `<lpf>` / `<hpf>` groups, then the shared ModControllableAudio tags). The value-override path is not
/// yet supported.
void serializeGlobalEffectableParamAttributes(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                              const int32_t* valuesForOverride = nullptr);
void serializeGlobalEffectableParamTags(const DocParamSet& unpatched, Serializer& out, bool writeAutomation,
                                        const int32_t* valuesForOverride = nullptr);

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

/// Write the `<midiParams>` block from the document's MIDI-CC params, byte-identical to
/// `MIDIParamCollection::writeToFile`: one `<param>` per CC holding a `<cc>` tag (the number, or "none" for
/// CC_NUMBER_NONE) and a `<value>` leaf (`writeDocAutoParam`). Nothing is emitted when there are no params.
void serializeMidiParams(const std::vector<DocMidiParam>& midiParams, Serializer& out, bool writeAutomation);

/// Read one AutoParam from the legacy hex blob into the document form — the read counterpart of
/// `writeDocAutoParam`. Reuses the engine's `AutoParam::readFromFile` (so the leaf hex parsing and its
/// back-compat are not reimplemented), then projects the result.
DocAutoParam readDocAutoParam(Deserializer& in, int32_t readAutomationUpToPos);

/// Read a `<patchCables>` block (current format) into the document's patch cables — the inverse of
/// `serializePatchCables`. Parses the structure directly (resolving range/depth-controlling cables back
/// to flat cables) and reads amounts via `readDocAutoParam`. Pre-V3.2 patch-cable quirks
/// (`rangeAdjustable` / placeholder-range resolution) are intentionally not carried over.
void deserializePatchCables(std::vector<DocPatchCable>& out, Deserializer& in, int32_t readAutomationUpToPos);

// --- DOM-native read of the owner param blocks (param/automation DOM Phase 5b) ---
// The read counterpart of the Phase 5a serializers. Each `read*ParamTag` is the table-driven DOM reader
// for one owner's param block: if `tagName` is a current-format param tag, it reads the leaf via
// `readDocAutoParam` into the live collection (and marks the summary's automation flag, matching
// `ParamSet::readParam`) and returns true; otherwise it returns false so the engine's remaining
// (legacy-only) dispatch can handle it. Reads land directly in the live manager (the authoritative DOM
// home is Phase 5c). Driven by the same `DocParamAttr` tables the serializers use.

/// Read one ModControllableAudio shared-block tag (the `<equalizer>` group + the flat MCA attrs).
bool readModControllableParamTag(Deserializer& in, char const* tagName, ParamSet& unpatched,
                                 ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos);

/// Read one GlobalEffectable param-block tag: the `<delay>`/`<lpf>`/`<hpf>` groups + the GE flat attrs,
/// then the shared MCA block. The `<lpf>`/`<hpf>` read groups also accept the legacy nested `morph`
/// member (which the writer emits as a flat `lpfMorph`/`hpfMorph` attr).
bool readGlobalEffectableParamTag(Deserializer& in, char const* tagName, ParamSet& unpatched,
                                  ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos);

/// Read one Sound param-block tag: the two unpatched flat attrs (portamento / compressorShape), the
/// patched flat attrs (osc / filter / LFO / FM / pitchAdjust family / community attrs), the four
/// `<envelope*>` groups, then the shared MCA block. The `<patchCables>` tag is NOT handled here — the
/// caller keeps the engine's reader for it (it carries pre-V3.2 cable back-compat the DOM reader drops).
bool readSoundParamTag(Deserializer& in, char const* tagName, ParamSet& patched, ParamCollectionSummary& patchedSummary,
                       ParamSet& unpatched, ParamCollectionSummary& unpatchedSummary, int32_t readAutomationUpToPos);

/// Read one ExpressionParamSet (MPE) tag (pitchBend / yExpression / pressure) into the live expression
/// collection. The legacy `channelPressure` alias is NOT handled here — the caller keeps it as a tail.
bool readExpressionParamTag(Deserializer& in, char const* tagName, ParamSet& expression,
                            ParamCollectionSummary& summary, int32_t readAutomationUpToPos);

// DOM-target variants (Phase 5c / DOM-live-authoritative): identical dispatch, but each leaf lands in a
// `DocParamSet` (the persistent DOM store) instead of a live engine `ParamSet`. `doc.params` must be
// pre-sized to the collection's param count; the engine is then reprojected from the store via
// `fromDocParamSet`. These share the same `DocParamAttr` tables as the engine-target readers above.

/// Read one ModControllableAudio shared-block tag into `doc` (the `<equalizer>` group + the flat MCA attrs).
bool readModControllableParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                      int32_t readAutomationUpToPos);

/// Read one GlobalEffectable param-block tag into `doc`: the `<delay>`/`<lpf>`/`<hpf>` groups + GE flat
/// attrs (incl. the legacy nested `morph` members) + the shared MCA block.
bool readGlobalEffectableParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                       int32_t readAutomationUpToPos);

/// Read one ExpressionParamSet (MPE) tag (pitchBend / yExpression / pressure) into `doc`, incl. the legacy
/// `channelPressure` alias (pressure / dimension 2) that the engine reader keeps as a caller tail.
bool readExpressionParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& doc,
                                 int32_t readAutomationUpToPos);

/// Read one Sound param-block tag into the two DOM stores: unpatched flat attrs + the shared MCA block land in
/// `unpatched`; patched flat attrs + the `<envelope1..4>` groups land in `patched`. `<patchCables>` is NOT handled
/// here — the caller keeps the engine reader for it (pre-V3.2 cable back-compat the DOM reader drops).
bool readSoundParamTagToDoc(Deserializer& in, char const* tagName, DocParamSet& patched, DocParamSet& unpatched,
                            int32_t readAutomationUpToPos);

// --- DOM-first editing (Phase 2 / 2b) ---
// The DOM store is the authoritative record; the engine param is the runtime it drives. A DOM-first edit
// mutates the store lane, then reprojects it into the engine so playback hears it (until the future
// graph-builder/executor split lets the runtime read the DOM directly, this reprojection is the seam).

/// DOM-first non-automated value edit: write `value` into the store's unpatched lane `p` (the authoritative
/// record), then reproject that lane into the engine `engineParam` (the runtime). Returns false (no-op) if the
/// store's unpatched set isn't populated or `p` is out of range. The caller performs the runtime notify/undo
/// (those are engine concerns, and this layer must not depend on the UI/action globals).
bool setUnpatchedValueDomFirst(DocParamManager& store, int32_t p, int32_t value, AutoParam& engineParam);

} // namespace deluge::project
