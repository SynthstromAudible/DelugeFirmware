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

#include "project/preset_document.h"

#include <ostream>

/// @file
/// @brief Prints a preset in a failed CppSpec comparison.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec.

namespace deluge::project {

/// @brief Prints a preset as its output's id and element, its drums' ids and its mapping count, e.g.
///        `{output 2 kit, drums [5 6], mappings 1}`.
/// @param os     The stream to print to.
/// @param preset The preset.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const PresetDocument& preset) {
	os << "{output ";
	for (const SongDocument::OutputEntry& entry : preset.output_order) {
		os << entry.id << ' ' << entry.element;
	}
	os << ", drums [";
	const char* gap = "";
	for (const auto& [id, drum] : preset.drums) {
		os << gap << id;
		gap = " ";
	}
	return os << "], mappings " << preset.mappings.entries().size() << '}';
}

} // namespace deluge::project
