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

#include "model/project_bridge/cable_bridge.h"

#include "instruments/deluge/cable_codec.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_serialization.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/value_tree_deserializer.h"

namespace deluge::bridge {

deluge::project::DocAutoParam amountOf(const deluge::serialization::Value& leaf, int32_t readAutomationUpToPos) {
	// The leaf parser reads a tag's value, so the leaf is read as the one entry of a map.
	deluge::serialization::Value holder = deluge::serialization::Value::makeMap();
	holder.set("amount", leaf);
	deluge::storage::ValueTreeDeserializer reader(holder);
	reader.match('{'); // enter the holder, as a JSON document's root is entered
	reader.readNextTagOrAttributeName();
	return readDocAutoParam(reader, readAutomationUpToPos);
}

bool containsSomething(const deluge::serialization::Value& leaf, int32_t readAutomationUpToPos) {
	const deluge::project::DocAutoParam amount = amountOf(leaf, readAutomationUpToPos);
	return !amount.nodes.empty() || amount.current_value != 0;
}

void loadCables(PatchCableSet& set, const deluge::serialization::Value& patchCables, int32_t readAutomationUpToPos) {
	const std::vector<deluge::instrument::CableState> cables = deluge::instrument::decode_cables(
	    patchCables, [readAutomationUpToPos](const deluge::serialization::Value& leaf) {
		    return containsSomething(leaf, readAutomationUpToPos);
	    });
	set.truncateCables(0);
	for (const deluge::instrument::CableState& state : cables) {
		const ParamDescriptor target =
		    ParamDescriptor(static_cast<int32_t>(cableWordOf(state.depth()))).getDestination();
		PatchCable* added =
		    set.appendCable({state.source, {target.data}, static_cast<deluge::synth::CablePolarity>(state.polarity)});
		if (added == nullptr) {
			break; // out of memory, or at kMaxNumPatchCables: keep the cables read so far
		}
		PatchCable& cable = *added;
		deluge::project::DocAutoParam amount = amountOf(*state.amount, readAutomationUpToPos);
		cable.param = AutoParam();
		cable.param.currentValue = amount.current_value;
		*set.docForParam(set.numPatchCables - 1) = std::move(amount);
	}
}

} // namespace deluge::bridge
