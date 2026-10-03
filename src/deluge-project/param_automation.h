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

/// Copy a value and its automation nodes into a DocAutoParam; works on the raw pieces so the model needs no engine
/// AutoParam.
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

/// Value-typed document form of one `ParamSet` — a parameter collection (patched / unpatched /
/// expression). `params` is dense, indexed by param id within the collection (`params[p]` is the doc
/// form of the live `ParamSet::params[p]`), so its size is the collection's parameter count.
struct DocParamSet {
	deluge::modulation::params::Kind kind{deluge::modulation::params::Kind::NONE};
	std::vector<DocAutoParam> params;
};

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

} // namespace deluge::project
