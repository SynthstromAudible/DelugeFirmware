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

#include "effects/bitcrush.h"

namespace deluge::effects {

BitcrushBlock Bitcrush::process(std::span<dsp::StereoSample> block, const BitcrushParams& params) {
	using dsp::fixed::BitDepth;
	using dsp::fixed::SampleRateReducer;
	const BitcrushBlock out{.volume_shift = BitDepth::volume_shift(params.bitcrush)};
	if (!SampleRateReducer::on(params.sample_rate_reduction)) {
		reducing_ = false;
		if (BitDepth::on(params.bitcrush)) {
			depth_.configure({.bitcrush = params.bitcrush});
			depth_.renderBlock(block, block);
		}
		return out;
	}
	if (!reducing_) {
		reducing_ = true;
		reducer_.on<&SampleRateReducer::start>();
	}
	reducer_.configure({
	    .sample_rate_reduction = params.sample_rate_reduction,
	    .grab_mask = BitDepth::mask(params.bitcrush, BitDepth::kGrabMaskBase),
	});
	reducer_.renderBlock(block, block);
	return out;
}

} // namespace deluge::effects
