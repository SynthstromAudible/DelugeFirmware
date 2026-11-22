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

#include "instruments/classic/cables.h"
#include "support/printers/param_id_printer.h"

#include <ostream>
#include <utility>

/// @file
/// @brief Prints a cable's route, its source and its destination param, in a failed CppSpec comparison, alone or as a
///        field of a record that holds one.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::instruments::classic {

/// @brief Prints a route as its source's number and its destination param, e.g. `{source 3, destination 25}`.
/// @param os The stream to print to.
/// @param r The route.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const CableRoute& r) {
	return os << "{source " << +std::to_underlying(r.source) << ", destination " << r.destination << "}";
}

} // namespace deluge::instruments::classic
