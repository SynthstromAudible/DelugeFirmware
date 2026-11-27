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

#include "project/controller_mapping.h"
#include "support/printers/param_id_printer.h"
#include "support/printers/param_target_printer.h"

#include <format>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints a sound's learned mod knobs (`ModKnobControl`, `ControllerMapping`) and whose controls they are
///        (`ControlsAddress`) in a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a mod knob as its button and its knob, e.g. `{button 3, knob 1}`.
/// @param os The stream to print to.
/// @param c The knob.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ModKnobControl& c) {
	return os << std::format("{{button {}, knob {}}}", c.button, c.knob);
}

/// @brief Prints a learned knob as its control and the param it moves, as param_target_printer.h prints a target.
/// @param os The stream to print to.
/// @param m The learned knob.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ControllerMapping& m) {
	return os << "{control " << m.control << ", target " << m.target << "}";
}

/// @brief Prints a controls owner as its kind's number and its id, e.g. `{kind 0, id 3}`.
/// @param os The stream to print to.
/// @param a The owner.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ControlsAddress& a) {
	return os << std::format("{{kind {}, id {}}}", std::to_underlying(a.kind), a.id);
}

} // namespace deluge::project
