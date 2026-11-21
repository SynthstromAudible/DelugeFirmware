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

#include "fakes.h"

#include <ostream>
#include <utility>

/// @file
/// @brief Prints one transport action the fake recorder kept, in a failed CppSpec comparison, alone or as a container
///        element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::scenes::bind::test {

/// @brief Prints a recorded action as its kind's value and its flag, e.g. `{kind 7, flag 1}`.
/// @param os   The stream to print to.
/// @param call The recorded action.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const RecordingTransportActions::Call& call) {
	return os << "{kind " << static_cast<int>(std::to_underlying(call.kind)) << ", flag " << call.flag << "}";
}

} // namespace deluge::scenes::bind::test
