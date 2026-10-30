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

#include <cstdint>

/// @brief One entry in the per-cluster-index waveform overview cache (issue #4460): the min/max peak
///        found while scanning a cluster's whole length, in the top 16 bits of a sample (see
///        toCachedPeak()), plus whether that scan has completed.
///
/// `Sample`-owned (in `Sample::overviewCache_`, model/sample/sample.h), one entry per cluster.
/// Deliberately its own header (rather than a nested `Sample` type): it has no dependency beyond
/// `<cstdint>`, so it stays includable from lightweight contexts (e.g. host unit tests) that can't
/// pull in `sample.h`'s full transitive closure.
struct OverviewCacheEntry {
	/// Running minimum peak found so far; starts at the maximum representable int16_t so any real
	/// sample immediately lowers it.
	int16_t min = INT16_MAX;
	/// Running maximum peak found so far; starts at the minimum representable int16_t so any real
	/// sample immediately raises it.
	int16_t max = INT16_MIN;
	/// Whether the whole-cluster scan for min/max has completed.
	bool investigated = false;
};
