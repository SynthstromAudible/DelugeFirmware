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

#include "dsp-next/core/fixed_point.h"
#include "effects/filter.h"
#include "effects/mod_fx.h"
#include "model/fx/conversion_memo.h"

#include <cstdint>

namespace deluge::model {

/// @brief The filter words and the oversampling allowance the filters' params follow from.
struct FilterWordsAndOversampling {
	effects::Filter::Words words;
	bool allow_oversampling;
	bool operator==(const FilterWordsAndOversampling&) const = default;
};

/// @brief A host chain's param conversions from their stored words to the values its block uses, each remembered
///        so a param that has not moved since the last block is not converted again. Render tier only.
struct ChainConversions {
	ConversionMemo<FilterWordsAndOversampling, effects::FilterParams> filter;
	ConversionMemo<effects::ModFx::Words, effects::ModFx::Params> mod_fx;
	ConversionMemo<int32_t, int32_t> delay_feedback;
	ConversionMemo<int32_t, int32_t> delay_rate;
	ConversionMemo<dsp::Q<31>, dsp::Q<27>> volume;
	ConversionMemo<dsp::Q<31>, dsp::Q<24>> pitch_adjust;
};

} // namespace deluge::model
