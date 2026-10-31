/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "synth/modulation/cable.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>

/// A routing's scope: the voice's cables (local params) or the sound's (global params). Unscoped: it indexes the
/// per-scope arrays throughout the app.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum Globality {
	GLOBALITY_LOCAL = 0,
	GLOBALITY_GLOBAL = 1,
};

namespace deluge::synth {

/// @brief One destination of a clip's routing: a param, or a cable's depth, and the run of cables [first_cable,
/// end_cable) that go to it. `sources` holds a bit per source whose change makes it recompute.
struct RoutedDestination {
	CableTarget target{};
	uint32_t sources{};
	uint8_t first_cable{};
	uint8_t end_cable{};
};

/// @brief One routed cable as the Patcher reads it: its source, polarity and which range value scales its depth.
struct RoutedCable {
	PatchSource source{PatchSource::NONE};
	CablePolarity polarity{CablePolarity::BIPOLAR};
	/// The range value (the Patcher's per-call range results, counted per globality) that scales this cable's depth;
	/// -1 for none.
	int8_t range_index{-1};
};

/// @brief The result of compiling one clip's cables: the order the cables take, and the routing in that order.
struct RoutingBuild {
	/// Position c takes the cable previously at order[c].
	std::array<uint8_t, kMaxCables> order{};
	uint8_t num_cables{};
	/// The cables [0, num_usable) are routed; the rest are kept but not patched.
	uint8_t num_usable{};
	/// Per globality (0 local, 1 global), sorted by their target word, then a destination with no target and no
	/// sources.
	std::array<std::array<RoutedDestination, kMaxCables + 1>, 2> destinations{};
	std::array<uint8_t, 2> num_destinations{};
	std::array<uint32_t, 2> sources_patched{};
	/// In the new order.
	std::array<RoutedCable, kMaxCables> cables{};
	/// A depth destination whose modulated destination is not routed: a state the runtime treats as a fault.
	bool missing_range_link{};
};

/// @brief One clip's compiled routing as the Patcher reads it: storage the instrument fits and keeps, viewed.
struct PatchRouting {
	/// Per globality, sorted by target word and ended by a destination with no sources; null when there are none.
	std::array<const RoutedDestination*, 2> destinations{};
	std::array<uint32_t, 2> sources_patched{};
	/// One per routed cable, in the routing's order.
	const RoutedCable* cables{};
};

/// @brief Compile @p cables as the runtime routes them: usable cables first (each unusable one swapped with the last
/// usable one after it), depth cables whose modulated cable is missing made unusable, each destination's cables grouped
/// in a run, the destinations of each globality sorted by their target word, and each depth destination linked to the
/// cable whose depth it scales.
/// @param cables The clip's cables in their current order.
/// @param usable Bit c: whether cable c may be patched.
/// @param out    The build, overwritten.
void compile_routing(std::span<const CableRecord> cables, std::bitset<kMaxCables> usable, RoutingBuild& out);

/// @brief Each cable's usability by @p usable, which is asked about the source nearest the param (the cable's own
/// source for a cable to a param) and the target, as the instrument judges a cable. No target is never usable.
template <class Usable>
[[nodiscard]] std::bitset<kMaxCables> usability_of(std::span<const CableRecord> cables, Usable&& usable) {
	std::bitset<kMaxCables> bits;
	for (size_t c = 0; c < cables.size(); ++c) {
		const CableRecord& cable = cables[c];
		if (cable.target.is_null()) {
			continue;
		}
		const PatchSource top = cable.target.top_source();
		bits[c] = usable(top == PatchSource::NOT_AVAILABLE ? cable.source : top, cable.target);
	}
	return bits;
}

} // namespace deluge::synth
