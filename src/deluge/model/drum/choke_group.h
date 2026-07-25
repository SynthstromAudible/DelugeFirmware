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
#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>

namespace deluge::drum {

/// A drum chokes when a note triggers a CHOKE-mode drum belonging to the same numbered choke
/// group (1..8). The triggering drum itself is always among the candidates Kit::choke() visits,
/// so when targetChokeGroup belongs to the triggering drum, this naturally also returns true for
/// it - that's what makes a CHOKE drum cut its own previous voice off on retrigger, with no
/// separate self-choke code path needed.
constexpr bool shouldChoke(PolyphonyMode targetPolyphonic, uint8_t targetChokeGroup, uint8_t triggeringChokeGroup) {
	return targetPolyphonic == PolyphonyMode::CHOKE && targetChokeGroup == triggeringChokeGroup;
}

/// Writes the "chokeGroup" attribute. Templated on the serializer type (and readChokeGroupFromFile
/// below on the deserializer type) purely so this shared read/write logic can be unit-tested on
/// the host with a lightweight fake, the same way readRoundRobinAlternates() is - see
/// tests/unit/choke_group_tests.cpp. Firmware code instantiates both with Serializer/Deserializer.
template <typename SerializerT>
void writeChokeGroupToFile(SerializerT& writer, uint8_t chokeGroup) {
	writer.writeAttribute("chokeGroup", chokeGroup);
}

template <typename DeserializerT>
uint8_t readChokeGroupFromFile(DeserializerT& reader) {
	uint8_t chokeGroup = reader.readTagOrAttributeValueInt();
	reader.exitTag("chokeGroup");
	return chokeGroup;
}

} // namespace deluge::drum
