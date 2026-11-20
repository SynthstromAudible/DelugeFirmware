/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "effects/words.h"
#include "util/misc.h"
#include <cstddef>

using deluge::effects::FilterMode;
constexpr int32_t kNumFilterModes = util::to_underlying(FilterMode::OFF) + 1;
constexpr FilterMode kLastLadder = FilterMode::TRANSISTOR_24DB_DRIVE;
using deluge::effects::kNumLPFModes;
constexpr FilterMode lastLpfMode = FilterMode::SVF_NOTCH;
constexpr FilterMode firstHPFMode = FilterMode::SVF_BAND;
using deluge::effects::FilterRoute;
using deluge::effects::kFirstHPFMode;
using deluge::effects::kNumHPFModes;

constexpr int32_t kNumFilterRoutes = util::to_underlying(FilterRoute::PARALLEL) + 1;

FilterRoute stringToFilterRoute(char const* string);
char const* filterRouteToString(FilterRoute route);

char const* lpfTypeToString(FilterMode lpfType);
FilterMode stringToLPFType(char const* string);

namespace deluge::model::filter {

using FilterFamily = deluge::effects::FilterFamily;
constexpr uint8_t kNumFamilies = 3;
enum LpLadderType { LP12, LP24, DRIVE };
constexpr uint8_t kNumLadders = 3;
enum SVFType { BAND, NOTCH };
constexpr uint8_t kNumSVF = 2;
enum HPFType { HP12 };
constexpr uint8_t kNumHPLadders = 1;

using specificFilterType = uint8_t;
// must match order of filter family declaration for indexing to work
constexpr uint8_t numForVariant[kNumFamilies] = {kNumLadders, kNumSVF, kNumHPLadders};

/// A filters family (types which can all share an implementation for toggling) and specific type within that family
class SpecificFilter {
public:
	SpecificFilter() = default;
	/// Construct a filtertype from a mode
	SpecificFilter(FilterMode mode) : family(effects::filter_family(mode)) {
		switch (mode) {
		case FilterMode::OFF:
			type = LpLadderType::LP12;
			break;
		case FilterMode::HPLADDER:
			type = HPFType::HP12;
			break;
		case FilterMode::SVF_BAND:
			type = SVFType::BAND;
			break;
		case FilterMode::SVF_NOTCH:
			type = SVFType::NOTCH;
			break;
		case FilterMode::TRANSISTOR_12DB:
			type = LpLadderType::LP12;
			break;
		case FilterMode::TRANSISTOR_24DB:
			type = LpLadderType::LP24;
			break;
		case FilterMode::TRANSISTOR_24DB_DRIVE:
			type = LpLadderType::DRIVE;
			break;
		}
	}

	FilterFamily getFamily() { return family; }

	specificFilterType getType() { return type; }

	SpecificFilter& incrementMode() {
		type = (type + 1) % numForVariant[util::to_underlying(family)];
		return *this;
	}

	FilterMode toMode() {
		int32_t baseNum = util::to_underlying(family);
		int32_t base = 0;
		for (int32_t i = 0; i < baseNum; i++) {
			int32_t famNum = numForVariant[i];
			base += famNum;
		}
		return static_cast<FilterMode>(base + type);
	}

private:
	FilterFamily family;
	specificFilterType type;
};

} // namespace deluge::model::filter
