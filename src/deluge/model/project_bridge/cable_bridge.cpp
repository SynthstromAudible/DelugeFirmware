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

#include "model/project_bridge/param_serialization.h"
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

} // namespace deluge::bridge
