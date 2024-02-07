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
#include "gui/l10n/strings.h"
#include "util/misc.h"

class Sound;

namespace deluge::dsp::filter {

enum class FilterFamily { LP_LADDER, SVF, HP_LADDER, NONE };
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
	SpecificFilter(FilterMode mode) {
		switch (mode) {
		case FilterMode::OFF:
			family = FilterFamily::NONE;
			type = LpLadderType::LP12;
			break;
		case FilterMode::HPLADDER:
			family = FilterFamily::HP_LADDER;
			type = HPFType::HP12;
			break;
		case FilterMode::SVF_BAND:
			family = FilterFamily::SVF;
			type = SVFType::BAND;
			break;
		case FilterMode::SVF_NOTCH:
			family = FilterFamily::SVF;
			type = SVFType::NOTCH;
			break;
		case FilterMode::TRANSISTOR_12DB:
			family = FilterFamily::LP_LADDER;
			type = LpLadderType::LP12;
			break;
		case FilterMode::TRANSISTOR_24DB:
			family = FilterFamily::LP_LADDER;
			type = LpLadderType::LP24;
			break;
		case FilterMode::TRANSISTOR_24DB_DRIVE:
			family = FilterFamily::LP_LADDER;
			type = LpLadderType::DRIVE;
			break;
		}
	}

	FilterFamily getFamily() { return family; }

	specificFilterType getType() { return type; }

	[[nodiscard]] l10n::String getMorphName() const {

		switch (family) {
		case FilterFamily::LP_LADDER:
			return l10n::String::STRING_FOR_DRIVE;
		case FilterFamily::HP_LADDER:
			return l10n::String::STRING_FOR_FM;
		case FilterFamily::SVF:
			return l10n::String::STRING_FOR_MORPH;
		default:
			return l10n::String::STRING_FOR_NONE;
		};
	};

	SpecificFilter& incrementMode() {
		type = (type + 1) % numForVariant[util::to_underlying(family)];
		return *this;
	} // namespace deluge::dsp::filter

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

} // namespace deluge::dsp::filter
