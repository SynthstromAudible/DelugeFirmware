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

#include "project/param_address.h"
#include "support/printers/optional_printer.h"
#include "support/printers/owner_id_printer.h"
#include "support/printers/param_id_printer.h"

#include <ostream>
#include <utility>

/// @file
/// @brief Prints an automation lane's address, its owner, space, param and clip, in a failed CppSpec comparison,
///        alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a param address as its owner, its space's number, its param and its clip, e.g.
///        `{{kind 2, id 7, row 0}, space 0, ParamId 24, clip none}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ParamAddress& a) {
	os << "{" << a.owner << ", space " << +std::to_underlying(a.space) << ", " << a.id << ", clip ";
	return deluge::test::printers::print_optional(os, a.clip_id) << "}";
}

} // namespace deluge::project
