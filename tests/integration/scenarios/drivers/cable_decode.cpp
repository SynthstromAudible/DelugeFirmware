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

// cable_decode <file> <SONG|SYNTH|KIT> — every patchCables element of the file, loaded as the app loads it (the Deluge
// instrument's decoder, then the bridge) into a cable set. One line per cable is printed (path, depth id, polarity,
// value, node count); the runner compares each file's lines with the fingerprint recorded from the app's reader.
// Exit: 0 loaded; 1 the file does not parse; 2 usage/IO.

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
		deluge::bridge::loadCables(set, *node, kAllAutomation);
		for (int32_t i = 0; i < set.numPatchCables; ++i) {
			const PatchCable& cable = set.patchCables[i];
			deluge::synth::CableTarget withSource = cable.destination;
			withSource = withSource.add_source(cable.from);
			const deluge::project::DocAutoParam& amount = *set.docForParam(i);
			std::fprintf(io.out, "%s %08x %d %d %d\n", path.c_str(),
			             deluge::bridge::cableDepthIdOf(withSource.word).value, static_cast<int>(cable.polarity),
			             amount.current_value, static_cast<int>(amount.nodes.size()));
		}
	}
	std::fprintf(io.err, "%zu patchCables elements loaded\n", found.size());
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(cable_decode, cable_decode_main)
