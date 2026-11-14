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

#include "scenes/bind/target.h"
#include "support/printers/optional_printer.h"
#include "support/printers/project_address_printer.h"

#include <optional>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints where the menu edits in a failed CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::scenes::bind {

/// @brief Prints the addresses field by field, an empty one as `none` and an enum as its number, e.g.
///        `{slot {kind 0, id 2}, chain none, chain_owner 2, kit_output none, affect_entire 0, clip {clip 7}, …}`.
/// @param os The stream to print to.
/// @param a The addresses.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Addresses& a) {
	using deluge::test::printers::print_optional;
	os << "{slot ";
	print_optional(os, a.slot) << ", chain ";
	print_optional(os, a.chain) << ", chain_owner " << +std::to_underlying(a.chain_owner) << ", kit_output ";
	print_optional(os, a.kit_output) << ", affect_entire " << int{a.affect_entire} << ", clip ";
	print_optional(os, a.clip) << ", track ";
	print_optional(os, a.track) << ", lane ";
	print_optional(os, a.lane) << ", note_row ";
	print_optional(os, a.note_row) << ", instrument " << +std::to_underlying(a.instrument);
	return os << "}";
}

} // namespace deluge::scenes::bind
