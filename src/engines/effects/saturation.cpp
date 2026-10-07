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

#include "engines/effects/saturation.h"

namespace deluge::effects {

void Saturation::process(std::span<dsp::StereoSample> block, const SaturationParams& params) {
	if (params.variant == SaturationVariant::CHAIN && params.clipping_amount == 0) {
		return;
	}
	for (dsp::StereoSample& sample : block) {
		sample.l = saturate(sample.l, 0, params);
		sample.r = saturate(sample.r, 1, params);
	}
}

} // namespace deluge::effects
