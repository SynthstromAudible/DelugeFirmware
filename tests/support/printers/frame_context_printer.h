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

#include "ui/widgets/core/frame_context.h"

#include <format>
#include <ostream>

/// @file
/// @brief Prints an input mask and an input claim set in a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::ui::widgets::core {

/// @brief Prints a mask as its grant flags, 1 for granted, e.g. `{turn 1, edit 0, horizontal 1, vertical 1, select 1,
///        back 1}`.
/// @param os The stream to print to.
/// @param m The mask.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const InputMask& m) {
	return os << std::format("{{turn {:d}, edit {:d}, horizontal {:d}, vertical {:d}, select {:d}, back {:d}}}", m.turn,
	                         m.edit, m.horizontal, m.vertical, m.select, m.back);
}

/// @brief Prints a claim set as its claim flags and its double-claim diagnostic, 1 for set.
/// @param os The stream to print to.
/// @param c The claims.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const InputClaims& c) {
	return os << std::format("{{turn {:d}, edit0 {:d}, edit1 {:d}, horizontal {:d}, vertical {:d}, "
	                         "select {:d}, back {:d}, double_claim {:d}}}",
	                         c.turn, c.edit0, c.edit1, c.horizontal, c.vertical, c.select, c.back, c.double_claim);
}

} // namespace deluge::ui::widgets::core
