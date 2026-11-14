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

#include "scenes/menu/shortcut_blink.h"

#include <cstddef>
#include <format>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints the menu's shortcut blinks in a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::scenes::menu {

/// @brief Prints a param pad's blink as its pad, whether it blinks, its frequency mask and its colour's number, e.g.
///        `{x 9, y 1, valid 1, freq_mask 1, colour 0}`.
/// @param os The stream to print to.
/// @param b The blink.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ParamBlink& b) {
	return os << std::format("{{x {}, y {}, valid {:d}, freq_mask {}, colour {}}}", b.x, b.y, b.valid, b.freq_mask,
	                         std::to_underlying(b.colour));
}

/// @brief Prints a source pad's blink as its frequency mask, its colour's number and whether it alternates, e.g.
///        `{freq_mask 255, colour 0, alternates 0}`.
/// @param os The stream to print to.
/// @param b The blink.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SourceBlink& b) {
	return os << std::format("{{freq_mask {}, colour {}, alternates {:d}}}", b.freq_mask, std::to_underlying(b.colour),
	                         b.alternates);
}

/// @brief Prints a blink plan as its param blink and every source pad's blink, column by column.
/// @param os The stream to print to.
/// @param p The plan.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const BlinkPlan& p) {
	os << "{param " << p.param << ", sources [";
	for (std::size_t sx = 0; sx < p.sources.size(); ++sx) {
		os << (sx == 0 ? "[" : ", [");
		for (std::size_t y = 0; y < p.sources[sx].size(); ++y) {
			os << (y == 0 ? "" : ", ") << p.sources[sx][y];
		}
		os << "]";
	}
	return os << "]}";
}

} // namespace deluge::scenes::menu
