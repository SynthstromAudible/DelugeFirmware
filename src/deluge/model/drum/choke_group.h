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

/// The valid range of choke group numbers. Shared by the CHOKE GROUP menu item (polyphony.h) and
/// the choke-group stem export granularity (stem_export.cpp) so both stay in sync with each other.
constexpr uint8_t kMinChokeGroup = 1;
constexpr uint8_t kMaxChokeGroup = 8;

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
/// the host with a lightweight fake, without pulling in Sound/Kit's full dependency graph - see
/// tests/unit/choke_group_tests.cpp. Firmware code instantiates both with Serializer/Deserializer.
template <typename SerializerT>
void writeChokeGroupToFile(SerializerT& writer, uint8_t chokeGroup) {
	// A missing attribute already reads back as group 1 - that's both SoundDrum's initialiser and
	// readChokeGroupFromFile()'s fallback - so writing the default would only add noise to every
	// drum of every kit, and make re-saving an untouched song look like it changed everywhere.
	// Key on the value rather than on PolyphonyMode::CHOKE: a drum put in group 3 and then switched
	// to POLY should still remember the 3 for when it goes back to CHOKE.
	if (chokeGroup == kMinChokeGroup) {
		return;
	}
	writer.writeAttribute("chokeGroup", chokeGroup);
}

template <typename DeserializerT>
uint8_t readChokeGroupFromFile(DeserializerT& reader) {
	// Read into a full int before validating - assigning straight to uint8_t would truncate first
	// (e.g. 257 -> 1) and make an invalid value look like a valid one.
	int32_t chokeGroup = reader.readTagOrAttributeValueInt();
	reader.exitTag("chokeGroup");
	// An out-of-range value can only come from a hand-edited or corrupted file. Fall back to group
	// 1 - the same group every pre-feature CHOKE drum lands in on load - so untrusted data behaves
	// like legacy data. An unvalidated group would choke like a private group but silently vanish
	// from choke-group stem export, whose arming loop only scans groups 1-8.
	if (chokeGroup < kMinChokeGroup || chokeGroup > kMaxChokeGroup) {
		return 1;
	}
	return (uint8_t)chokeGroup;
}

/// Counts how many of the 8 groups actually bundle drums together - hold more than one exportable
/// drum. Only those are worth rendering as a single stem: a group of one has nothing to choke it,
/// so its drum is exported on its own like any other, and rendering it "as a group" would produce
/// exactly the same audio under a more confusing name.
///
/// This is the count that decides both whether choke-group export is worth offering at all and how
/// the kit is divided up when it runs. Note what the caller's predicate has to count: only drums
/// actually in CHOKE mode belong to a group. Every drum carries a chokeGroup, defaulting to 1
/// whether or not it is in CHOKE mode, so counting stored numbers alone would sweep every Poly,
/// Mono and Auto drum in the kit into group 1.
///
/// Templated on a counting predicate rather than taking a collection directly, so the caller
/// doesn't need to materialize a list of every drum's group into a buffer first: stem_export.cpp
/// passes a closure that scans its own NoteRows for a given group number. This also keeps the
/// counting itself host-testable with a trivial fake, without needing a live
/// Kit/InstrumentClip/NoteRow - see tests/unit/choke_group_tests.cpp.
template <typename CountDrumsInGroupFn>
uint8_t countBundlingChokeGroups(CountDrumsInGroupFn countDrumsInGroup) {
	uint8_t count = 0;
	for (uint8_t group = kMinChokeGroup; group <= kMaxChokeGroup; group++) {
		if (countDrumsInGroup(group) > 1) {
			count++;
		}
	}
	return count;
}

} // namespace deluge::drum
