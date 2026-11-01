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

#include "synth/modulation/patch_routing.h"

#include "modulation/params/param.h" // params::FIRST_GLOBAL

#include <algorithm>
#include <utility>

namespace deluge::synth {

namespace {
uint32_t source_bit(PatchSource s) {
	return uint32_t{1} << std::to_underlying(s);
}
} // namespace

void compile_routing(std::span<const CableRecord> cables, std::bitset<kMaxCables> usable, RoutingBuild& out) {
	out = {};
	const auto n = static_cast<int32_t>(cables.size());
	out.num_cables = static_cast<uint8_t>(n);
	std::array<uint8_t, kMaxCables>& at = out.order;
	for (int32_t c = 0; c < n; ++c) {
		at[c] = static_cast<uint8_t>(c);
	}
	auto cable = [&](int32_t c) -> const CableRecord& { return cables[at[c]]; };
	auto index_of = [&](PatchSource s, CableTarget t) {
		for (int32_t c = 0; c < n; ++c) {
			if (cable(c).source == s && cable(c).target == t) {
				return c;
			}
		}
		return int32_t{-1};
	};

	// Usable cables first: an unusable one swaps with the last usable cable after it.
	int32_t potentially = n;
	int32_t num_usable = 0;
	for (; num_usable < potentially; ++num_usable) {
		if (usable[at[num_usable]]) {
			continue;
		}
		bool found = false;
		while (--potentially > num_usable) {
			if (usable[at[potentially]]) {
				std::swap(at[potentially], at[num_usable]);
				found = true;
				break;
			}
		}
		if (!found) {
			break;
		}
	}

	// A depth cable whose modulated cable does not exist is unusable: it swaps with the last usable cable.
	for (int32_t c = 0; c < num_usable;) {
		const CableTarget t = cable(c).target;
		if (!t.is_param() && index_of(t.bottom_source(), t.modulated()) < 0) {
			--num_usable;
			if (num_usable == c) {
				break;
			}
			std::swap(at[c], at[num_usable]);
			continue;
		}
		++c;
	}
	out.num_usable = static_cast<uint8_t>(num_usable);

	// Group each destination's cables into one run.
	std::array<int32_t, 2> count{};
	for (int32_t c = 0; c < num_usable;) {
		const CableTarget t = cable(c).target;
		const int32_t g = t.param_index() < modulation::params::FIRST_GLOBAL ? 0 : 1;
		RoutedDestination& d = out.destinations[g][count[g]];
		d.target = t;
		d.first_cable = static_cast<uint8_t>(c);
		uint32_t sources = source_bit(cable(c).source);
		for (int32_t c2 = c + 1; c2 < num_usable; ++c2) {
			if (cable(c2).target == t) {
				sources |= source_bit(cable(c2).source);
				++c;
				if (c2 != c) {
					std::swap(at[c2], at[c]);
				}
			}
		}
		++c;
		d.end_cable = static_cast<uint8_t>(c);
		d.sources = sources;
		out.sources_patched[g] |= sources;
		++count[g];
	}

	for (int32_t c = 0; c < n; ++c) {
		out.cables[c].source = cable(c).source;
		out.cables[c].polarity = cable(c).polarity;
	}

	for (int32_t g = 0; g < 2; ++g) {
		out.num_destinations[g] = static_cast<uint8_t>(count[g]);
		if (count[g] == 0) {
			continue;
		}
		std::span<RoutedDestination> list{out.destinations[g].data(), static_cast<size_t>(count[g])};
		out.destinations[g][count[g]] = {};
		// The runtime sorts by the word read as a signed number; every word it routes is unique within a globality.
		std::ranges::sort(list, {}, [](const RoutedDestination& d) { return static_cast<int32_t>(d.target.word); });

		int8_t i = 0;
		for (size_t k = 0; k < list.size() && list[k].target.word < 0xFFFF'FF00u; ++k) {
			RoutedDestination& d = list[k];
			const CableTarget modulated = d.target.modulated();
			if (const int32_t c = index_of(d.target.bottom_source(), modulated); c >= 0) {
				out.cables[c].range_index = i;
			}
			size_t m = k + 1;
			while (m < list.size() && list[m].target != modulated) {
				++m;
			}
			if (m >= list.size()) {
				out.missing_range_link = true;
			}
			else {
				list[m].sources |= d.sources;
			}
			d.sources |= source_bit(d.target.bottom_source());
			++i;
		}
	}
}

} // namespace deluge::synth
