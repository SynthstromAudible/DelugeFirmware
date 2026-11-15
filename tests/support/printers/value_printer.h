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

#include "serialization/value.h"
#include "serialization/write_json.h"

#include <ostream>
#include <string>

/// @file
/// @brief Prints a document node in a failed CppSpec comparison, alone or as a field of a record that holds one.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::serialization {

/// @brief Prints a node as its JSON on one line, e.g. `{ "muteMidiCommand": { "channel": 0, "note": 2 } }`.
/// @param os The stream to print to.
/// @param v The node.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Value& v) {
	std::string json;
	for (const char c : write_json(v)) {
		if (c == '\n') {
			json += ' ';
		}
		else if (c != '\t') {
			json += c;
		}
	}
	while (!json.empty() && json.back() == ' ') {
		json.pop_back();
	}
	return os << json;
}

} // namespace deluge::serialization
