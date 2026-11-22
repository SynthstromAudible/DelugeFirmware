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

#include "support/printers/controller_mapping_printer.h"
#include "support/printers/owner_id_printer.h"
#include "ui/controls/facts.h"

#include <ostream>
#include <utility>

/// @file
/// @brief Prints the facts a learned dispatch reads (a playing output and a kit's sound row) in a failed CppSpec
///        comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::ui::controls {

/// @brief Prints a playing output's kind as its number, e.g. `output_kind 1`.
/// @param os The stream to print to.
/// @param kind The kind.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, PlayingOutput::Kind kind) {
	return os << "output_kind " << +std::to_underlying(kind);
}

/// @brief Prints a playing output field by field, e.g.
///        `{owner {kind 1, id 3}, param_owner {kind 2, id 3, row 0}, output_kind 0, clip 7}`.
/// @param os The stream to print to.
/// @param o The output.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const PlayingOutput& o) {
	return os << "{owner " << o.owner << ", param_owner " << o.param_owner << ", " << o.kind << ", clip " << o.clip_id
	          << "}";
}

/// @brief Prints a kit's sound row as its drum and its params' owner, e.g. `{drum 4, param_owner {kind 3, id 4, row
/// 0}}`.
/// @param os The stream to print to.
/// @param r The row.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const KitRow& r) {
	return os << "{drum " << r.drum_id << ", param_owner " << r.param_owner << "}";
}

} // namespace deluge::ui::controls
