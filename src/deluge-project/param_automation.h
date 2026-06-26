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
#include <vector>

class AutoParam;
class ParamSet;
class ParamManager;
class Serializer;
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

/// Write the `<patchCables>` block from the document's patch cables, byte-identical to
/// `PatchCableSet::writePatchCablesToFile` (including the depth-controlling / range-cable nesting).
void serializePatchCables(const std::vector<DocPatchCable>& cables, Serializer& out, bool writeAutomation);

} // namespace deluge::project
