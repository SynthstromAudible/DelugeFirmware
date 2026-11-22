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

#include "ui/gfx/oled/geometry.h"

#include <format>
#include <ostream>

/// @file
/// @brief Prints an OLED point and rectangle in a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::ui::gfx::oled {

/// @brief Prints a point as its coordinates, e.g. `{x 20, y 20}`.
/// @param os The stream to print to.
/// @param p The point.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Point& p) {
	return os << std::format("{{x {}, y {}}}", p.x, p.y);
}

/// @brief Prints a rectangle as its top-left corner and extent, e.g. `{x 0, y 14, w 128, h 34}`.
/// @param os The stream to print to.
/// @param r The rectangle.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Rect& r) {
	return os << std::format("{{x {}, y {}, w {}, h {}}}", r.x, r.y, r.w, r.h);
}

} // namespace deluge::ui::gfx::oled
