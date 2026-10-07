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
	const uint32_t amount = params.clipping_amount;
	if (params.variant == SaturationVariant::CHAIN && amount == 0) {
		return;
	}
	const bool voice = params.variant == SaturationVariant::VOICE;
	const uint32_t drive = voice ? 5 + amount : 3 + amount;
	const uint32_t shift = voice ? (amount >= 2 ? amount - 2 : 0) : (amount >= 3 ? amount - 3 : 0);
	channels_[0].setDrive(drive);
	channels_[1].setDrive(drive);
	for (dsp::StereoSample& sample : block) {
		sample.l = channels_[0].render(sample.l) << shift;
		sample.r = channels_[1].render(sample.r) << shift;
	}
}

} // namespace deluge::effects
