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

#include "ui/scenes/bind/ports/sound_writes.h"

#include <format>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints why a source's ranges are written in a failed CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::ui::scenes::bind {

/// @brief Prints a cause as its kind's number and the moved marker, e.g. `{kind 1, marker 3}`.
/// @param os The stream to print to.
/// @param c The cause.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const RangesCause& c) {
	return os << std::format("{{kind {}, marker {}}}", std::to_underlying(c.kind), std::to_underlying(c.marker));
}

} // namespace deluge::ui::scenes::bind
