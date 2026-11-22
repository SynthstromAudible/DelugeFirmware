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

#include "project/fx_chain.h"
#include "support/printers/state_handle_printer.h"

#include <ostream>

/// @file
/// @brief Prints a host owner's effect chain in a failed CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a chain's slot as its effect's id, e.g. `"delay"`.
/// @param os The stream to print to.
/// @param s The slot.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const FxSlot& s) {
	return os << '"' << s.effect << '"';
}

/// @brief Prints a chain field by field, an empty reverb return as `none`, e.g.
///        `{slots ["delay" "reverb"], reverb_return 1, effects {plugin …}, mod_fx_param 1, filter_type 0}`.
/// @param os The stream to print to.
/// @param c The chain.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const FxChain& c) {
	os << "{slots [";
	const char* separator = "";
	for (const FxSlot& s : c.slots) {
		os << separator << s;
		separator = " ";
	}
	os << "], reverb_return ";
	if (c.reverb_return) {
		os << *c.reverb_return;
	}
	else {
		os << "none";
	}
	return os << ", effects " << c.effects << ", mod_fx_param " << int{c.mod_fx_param} << ", filter_type "
	          << int{c.filter_type} << "}";
}

} // namespace deluge::project
