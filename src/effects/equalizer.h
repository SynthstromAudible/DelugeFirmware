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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/filter/shelf_pair.h"

#include <span>

namespace deluge::effects {

/// @brief One block's EQ settings: the four params' Q31 values (a band at 0 is flat).
using EqualizerParams = dsp::fixed::ShelfPair::Settings;

/// @brief The EQ: bass and treble shelves; does nothing while both bands are flat.
class Equalizer {
public:
	/// @brief Applies the bass and treble shelves to @p block in place; does nothing while both bands are flat.
	/// @param block The stereo block to process.
	/// @param params The block's EQ settings.
	void process(std::span<dsp::StereoSample> block, const EqualizerParams& params) {
		if (dsp::fixed::ShelfPair::flat(params)) {
			return;
		}
		shelves_.configure(params);
		shelves_.renderBlock(block, block);
	}

private:
	dsp::Mono<dsp::fixed::ShelfPair> shelves_; ///< The two splits' memories.
};

} // namespace deluge::effects
