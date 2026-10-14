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

#include "dsp-next/fixed/vca/pan.h"
#include "util/fixedpoint.h"

namespace deluge::effects {

bool SendAndVolume::process(std::span<dsp::StereoSample> block, std::span<int32_t> reverb,
                            const SendAndVolumeParams& params) {
	const int32_t send_gain = multiply_32x32_rshift32(params.post_fx_volume, params.reverb_send) << 5;

	int32_t gain_l = multiply_32x32_rshift32(params.post_reverb_volume, params.post_fx_volume) << 5;
	int32_t gain_r = gain_l;

	// The post-reverb volume ramps across the block from the last block's, so a sidechain's duck through it is smooth.
	int32_t step_l = 0;
	int32_t step_r = 0;
	if (params.ramp) {
		const auto volume_step =
		    static_cast<int32_t>(static_cast<double>(params.post_reverb_volume - last_post_reverb_volume_)
		                         / static_cast<double>(block.size()));
		step_l = step_r = multiply_32x32_rshift32(params.post_fx_volume, volume_step) << 5;
	}

	if (params.pan != 0 && params.stereo) {
		const dsp::fixed::PanAmplitudes pan = dsp::fixed::pan_amplitudes(params.pan);
		gain_l = multiply_32x32_rshift32(gain_l, pan.left) << 2;
		gain_r = multiply_32x32_rshift32(gain_r, pan.right) << 2;
		step_l = multiply_32x32_rshift32(step_l, pan.left) << 2;
		step_r = multiply_32x32_rshift32(step_r, pan.right) << 2;
	}

	auto reverb_out = reverb.begin();
	for (dsp::StereoSample& sample : block) {
		if (params.reverb_send != 0) {
			*(reverb_out++) += multiply_32x32_rshift32(sample.l + sample.r, send_gain) << 1;
		}
		if (params.ramp) {
			gain_l += step_l;
			gain_r += step_r;
		}
		sample.l = multiply_32x32_rshift32(sample.l, gain_l) << 5;
		sample.r = multiply_32x32_rshift32(sample.r, gain_r) << 5;
	}

	last_post_reverb_volume_ = params.post_reverb_volume;
	return params.reverb_send != 0;
}

} // namespace deluge::effects
