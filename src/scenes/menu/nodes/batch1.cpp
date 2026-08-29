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

/// @file
/// @brief The one place the six batch-1 tables are joined: the seventeen roots, and the two
///        expected-flag sets the validator audits against.
///
/// Its own translation unit rather than an arbitrary one of the six, because each of the three
/// lists spans several of them (the roots span all six; the half-precision set spans the
/// oscillator and compressor tables) and picking a host for them inside one table would make that
/// table quietly special. Nothing here declares a `Node`; it only concatenates spans the tables
/// export.
///
/// The order is `src/deluge/gui/menu_item/generate/main.py`'s own `top_level_menus`: the filters,
/// the compressor, the unison page, then the oscillator, envelope and LFO lists. That is the order
/// `assign_node_ids` walks, and therefore the order every node id in this generation derives from.

#include "scenes/menu/nodes/nodes.h"

#include <array>

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief Copy every element of @p from into @p out at @p at, advancing @p at.
void append(std::span<const Node*> out, size_t& at, std::span<const Node* const> from) {
	for (const Node* n : from) {
		if (at < out.size()) {
			out[at++] = n;
		}
	}
}

/// @brief 1 + 1 + 1 + 6 + 4 + 4.
constexpr size_t kRootCount = 17;
/// @brief Ten in the oscillator subtree (`sound_osc.cpp`'s `kRangeDependent`), none elsewhere.
constexpr size_t kRangeDependentCount = 10;
/// @brief Two pulse widths plus the compressor threshold.
constexpr size_t kHalfPrecisionCount = 3;

} // namespace

std::span<const Node* const> batch1_roots() {
	static const std::array<const Node*, kRootCount> roots = [] {
		std::array<const Node*, kRootCount> out{};
		size_t at = 0;
		append(out, at, kSoundFiltersChildren);
		append(out, at, kSoundCompressorChildren);
		append(out, at, kSoundUnisonChildren);
		append(out, at, kSoundOscChildren);
		append(out, at, kSoundEnvChildren);
		append(out, at, kSoundLfoChildren);
		return out;
	}();
	return roots;
}

std::span<const Node* const> kRangeDependentNodes() {
	static const std::array<const Node*, kRangeDependentCount> nodes = [] {
		std::array<const Node*, kRangeDependentCount> out{};
		size_t at = 0;
		append(out, at, kOscRangeDependentNodes);
		return out;
	}();
	return nodes;
}

std::span<const Node* const> kHalfPrecisionNodes() {
	static const std::array<const Node*, kHalfPrecisionCount> nodes = [] {
		std::array<const Node*, kHalfPrecisionCount> out{};
		size_t at = 0;
		append(out, at, kOscHalfPrecisionNodes);
		append(out, at, kCompressorHalfPrecisionNodes);
		return out;
	}();
	return nodes;
}

} // namespace deluge::scenes::menu::nodes
