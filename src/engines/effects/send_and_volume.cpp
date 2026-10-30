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

#include "engines/effects/send_and_volume.h"

#include "dsp-next/core/wrapping.h"
#include "util/fixedpoint.h"

#include <cstddef>

namespace deluge::effects {

bool SendAndVolume::process(std::span<dsp::StereoSample> block, std::span<int32_t> reverb,
                            const SendAndVolumeParams& params) {
	const bool sent = params.reverb_send != 0;
	if (sent) {
		// The send reads each frame before its volume, so it runs first, as its own pass.
		const int32_t send_gain = multiply_32x32_rshift32(params.post_fx_volume, params.reverb_send)
		                          << dsp::fixed::kVolumeShift;
		for (std::size_t i = 0; i < block.size(); ++i) {
			reverb[i] = dsp::wrapping_add(reverb[i], dsp::fixed::reverb_send(block[i], send_gain));
		}
	}
	gains_.configure({
	    .post_fx_volume = params.post_fx_volume,
	    .post_reverb_volume = params.post_reverb_volume,
	    .pan = params.pan,
	    .ramp = params.ramp,
	    .stereo = params.stereo,
	});
	gains_.renderBlock(block, block);
	return sent;
}

} // namespace deluge::effects
