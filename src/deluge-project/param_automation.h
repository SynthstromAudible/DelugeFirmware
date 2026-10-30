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

#include "deluge-project/param_node.h"
#include "plugin-api/types/param.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace deluge::project {

/// @brief Value-typed document form of one `AutoParam` — the automation leaf of the param/automation DOM
/// (docs/dev/param_automation_dom.md).
///
/// `ParamNode` is a `{pos, value, interpolated}` POD; an empty `nodes` means the parameter is unautomated and
/// `current_value` applies. Patch-cable AutoParams store their node values in the patch-cable range — the projection
/// copies the stored representation faithfully (no reinterpretation).
struct DocAutoParam {
	int32_t current_value{0};
	ParamNodeVector nodes; // OrderedPosVector<ParamNode>: the single runtime store the engine borrows
};

/// @brief Copy a value and its automation nodes into a DocAutoParam; works on the raw pieces so the model needs no
/// engine AutoParam.
/// @param current_value The parameter's current value.
/// @param nodes         Its automation nodes, ascending by position.
/// @return The document form. If allocation fails part-way, it holds the nodes copied so far.
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

/// @brief Copy a DocAutoParam's value and nodes out into the raw pieces.
/// @param doc           The document form.
/// @param current_value Receives the current value.
/// @param nodes         Receives the nodes; a no-op when it is @p doc's own node store (the engine borrows it).
inline void fromDocAutoParam(const DocAutoParam& doc, int32_t& current_value, ParamNodeVector& nodes) {
	current_value = doc.current_value;
	if (&nodes == &doc.nodes) {
		// Borrowed: the engine's node store IS doc.nodes, so a copy would clear-then-read the same
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

/// @brief Value-typed document form of one param collection: lanes for a dense run of ids, `params[i]` holding the
/// param with id `first + i`.
struct DocParamSet {
	deluge::plugin::ParamId first{};
	std::vector<DocAutoParam> params;

	/// @brief The lane for @p id, or nullptr when this set does not hold it.
	/// @param id The param id.
	/// @return The lane, or nullptr.
	[[nodiscard]] DocAutoParam* find(deluge::plugin::ParamId id) {
		auto i = indexOf(id);
		return i ? &params[*i] : nullptr;
	}
	[[nodiscard]] const DocAutoParam* find(deluge::plugin::ParamId id) const {
		auto i = indexOf(id);
		return i ? &params[*i] : nullptr;
	}
	/// @brief Where @p id's lane is in `params`, or nullopt when this set does not hold it.
	/// @param id The param id.
	/// @return The index, or nullopt.
	[[nodiscard]] std::optional<size_t> indexOf(deluge::plugin::ParamId id) const {
		if (id.value < first.value || id.value - first.value >= params.size()) {
			return std::nullopt;
		}
		return static_cast<size_t>(id.value - first.value);
	}
};

/// @brief Value-typed document form of one patch cable: the engine's routing, named by the id of the cable's depth (the
/// engine derives it from the cable's source and destination), with its polarity (the engine's word, stored raw) and
/// the depth's value and automation. Depths are stored in the patch-cable range [-2^30, 2^30).
struct DocCable {
	deluge::plugin::ParamId depth{};
	uint8_t polarity{};
	DocAutoParam amount;
};

/// @brief Value-typed document form of one MIDI-CC automation lane: the CC number that identifies it (the ordered
/// MIDIParamVector keys by cc) plus its automatable value. Keyed by identity (cc), like patch cables by
/// (from,dest) — the CC vector reorders on every learn, so index is not stable.
struct DocMidiParam {
	uint8_t cc{};
	DocAutoParam param;
};

/// @brief How many runs of ids an engine's params are stored in.
inline constexpr std::size_t kEngineParamBlocks = 2;

/// @brief Value-typed document form of a `ParamManager`'s collections. An engine's params are held in the runs of ids
/// its table declares, each present only when the live owner holds it, and the engine's cables, each with its depth;
/// `expression` (MPE) and `midi_params` (a MIDI_OUT clip's CC automation) are the host's own spaces.
struct DocParamManager {
	std::array<std::optional<DocParamSet>, kEngineParamBlocks> engine_params;
	std::optional<DocParamSet> expression;
	std::vector<DocCable> cables;
	std::vector<DocMidiParam> midi_params;

	/// @brief The lane of the engine param @p id (a param in a block, or a cable's depth), or nullptr when none holds
	/// it.
	/// @param id The engine param id.
	/// @return The lane, or nullptr.
	[[nodiscard]] DocAutoParam* findEngineParam(deluge::plugin::ParamId id) {
		for (std::optional<DocParamSet>& block : engine_params) {
			if (block) {
				if (DocAutoParam* lane = block->find(id)) {
					return lane;
				}
			}
		}
		for (DocCable& cable : cables) {
			if (cable.depth == id) {
				return &cable.amount;
			}
		}
		return nullptr;
	}
};

} // namespace deluge::project
