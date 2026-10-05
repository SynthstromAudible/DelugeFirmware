/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// cable_decode <file> <SONG|SYNTH|KIT> — every patchCables element of the file, read by the app's reader into a
// cable set, holds the cables, depths, polarities and amounts the Deluge instrument's decoder gives. One line per cable
// is printed (path, depth id, polarity, value, node count) for the runner to fingerprint. Exit: 0 agree; 1 the file
// does not parse; 2 usage/IO; 3 a cable differs (printed).

#include "engines/deluge/cable_codec.h"
#include "model/project_bridge/cable_bridge.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/document_format.h"
#include "storage/value_tree_deserializer.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;

constexpr int32_t kAllAutomation = 2147483647;

/// Every patchCables node under @p node, with its path.
void collect(const Value& node, const std::string& path, std::vector<std::pair<std::string, const Value*>>& into) {
	if (node.isMap()) {
		for (const auto& [key, child] : node.entries()) {
			const std::string at = path + "/" + key;
			if (key == "patchCables") {
				into.emplace_back(at, &child);
			}
			else {
				collect(child, at, into);
			}
		}
	}
	else if (node.isArray()) {
		for (size_t i = 0; i < node.items().size(); ++i) {
			collect(node.items()[i], path + "/" + std::to_string(i), into);
		}
	}
}

/// The cables of @p patchCables as the app's reader leaves them in a cable set, reading a tree parsed from a
/// document of @p format as the app reads it.
void readLikeTheApp(const Value& patchCables, deluge::storage::SourceFormat format, PatchCableSet& set) {
	Value holder = Value::makeMap();
	holder.set("patchCables", patchCables);
	deluge::storage::ValueTreeDeserializer reader(holder, format);
	reader.match('{'); // enter the holder, as a JSON document's root is entered
	reader.readNextTagOrAttributeName();
	set.readPatchCablesFromFile(reader, kAllAutomation);
}

bool sameNodes(const deluge::project::DocAutoParam& a, const deluge::project::DocAutoParam& b) {
	if (a.current_value != b.current_value || a.nodes.size() != b.nodes.size()) {
		return false;
	}
	for (int32_t i = 0; i < static_cast<int32_t>(a.nodes.size()); ++i) {
		if (a.nodes[i].pos != b.nodes[i].pos || a.nodes[i].value != b.nodes[i].value
		    || a.nodes[i].interpolated != b.nodes[i].interpolated) {
			return false;
		}
	}
	return true;
}

int cable_decode_main(int argc, char** argv, Io io) {
	if (argc < 3) {
		std::fprintf(io.err, "usage: %s <file> <SONG|SYNTH|KIT>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string text;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		text.append(chunk, n);
	}
	std::fclose(f);
	const deluge::storage::DocumentFormat format = deluge::storage::formatOfPath(argv[1]);
	std::optional<Value> tree = parseDocument(text, format);
	if (!tree) {
		std::fprintf(io.err, "LOADFAIL does not parse\n");
		return 1;
	}
	std::vector<std::pair<std::string, const Value*>> found;
	collect(*tree, "", found);
	for (const auto& [path, node] : found) {
		ParamManagerForTimeline manager;
		manager.setupWithPatching();
		PatchCableSet& set = *manager.getPatchCableSet();
		readLikeTheApp(*node, sourceFormatOf(format), set);
		std::vector<deluge::instrument::CableState> decoded = deluge::instrument::decode_cables(
		    *node, [](const Value& leaf) { return deluge::bridge::containsSomething(leaf, kAllAutomation); });
		if (static_cast<size_t>(set.numPatchCables) != decoded.size()) {
			std::fprintf(io.err, "CABLE DIFF %s: the app keeps %d cables, the decoder %zu\n", path.c_str(),
			             set.numPatchCables, decoded.size());
			return 3;
		}
		for (size_t i = 0; i < decoded.size(); ++i) {
			const PatchCable& cable = set.patchCables[i];
			ParamDescriptor withSource = cable.destinationParamDescriptor;
			withSource.addSource(cable.from);
			const uint32_t appDepth = deluge::bridge::cableDepthIdOf(withSource.data).value;
			const uint32_t ourDepth = decoded[i].depth().value;
			const deluge::project::DocAutoParam ourAmount =
			    deluge::bridge::amountOf(*decoded[i].amount, kAllAutomation);
			const deluge::project::DocAutoParam& appAmount = *set.docForParam(static_cast<int32_t>(i));
			if (appDepth != ourDepth) {
				std::fprintf(io.err, "CABLE DIFF %s cable %zu: depth %08x, decoder %08x\n", path.c_str(), i, appDepth,
				             ourDepth);
				return 3;
			}
			if (static_cast<int>(cable.polarity) != static_cast<int>(decoded[i].polarity)) {
				std::fprintf(io.err, "CABLE DIFF %s cable %zu: polarity\n", path.c_str(), i);
				return 3;
			}
			if (!sameNodes(appAmount, ourAmount)) {
				std::fprintf(io.err, "CABLE DIFF %s cable %zu: amount\n", path.c_str(), i);
				return 3;
			}
			std::fprintf(io.out, "%s %08x %d %d %d\n", path.c_str(), ourDepth, static_cast<int>(cable.polarity),
			             appAmount.current_value, static_cast<int>(appAmount.nodes.size()));
		}
	}
	std::fprintf(io.err, "%zu patchCables elements agree\n", found.size());
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(cable_decode, cable_decode_main)
