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

#include "project/clip_values.h"
#include "support/printers/arp_record_printer.h"

#include <optional>
#include <ostream>
#include <string_view>

/// @file
/// @brief Prints a clip's values in a failed CppSpec comparison.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec.

namespace deluge::project {

/// @brief Prints a clip's values, `-` for one it does not hold, e.g.
///        `{midiBank -, midiSub -, midiPGM 12, bendRange -, bendRangeMPE 48, arp {…}}`.
/// @param os     The stream to print to.
/// @param values The clip's values.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ClipValues& values) {
	const auto one = [&](std::string_view name, const std::optional<int32_t>& value) {
		os << name << ' ';
		value ? os << *value : os << '-';
	};
	os << '{';
	one("midiBank", values.midi_bank);
	os << ", ";
	one("midiSub", values.midi_sub);
	os << ", ";
	one("midiPGM", values.midi_pgm);
	os << ", ";
	one("bendRange", values.bend_range);
	os << ", ";
	one("bendRangeMPE", values.bend_range_mpe);
	os << ", arp ";
	values.arp ? os << *values.arp : os << '-';
	return os << '}';
}

} // namespace deluge::project
