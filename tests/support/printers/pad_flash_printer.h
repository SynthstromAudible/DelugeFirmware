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

#include "ui/widgets/grid/frame.h"

#include <format>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints a pad flash in a failed CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and cannot print a container
/// of a type it cannot stream. The operator lives in the type's own namespace so that lookup finds it from inside
/// CppSpec. Every spec that compares pad flashes includes this header: whether a type streams is decided once per type
/// in a binary, so a spec linked with others that print the same type must agree with them.
namespace deluge::ui::widgets::grid {

/// @brief Prints a pad flash as its column, row and colour code, e.g. `{x 15, y 6, colour 6}`.
/// @param os The stream to print to.
/// @param f The flash.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const PadFlash& f) {
	return os << std::format("{{x {}, y {}, colour {}}}", f.x, f.y, std::to_underlying(f.colour));
}

} // namespace deluge::ui::widgets::grid
