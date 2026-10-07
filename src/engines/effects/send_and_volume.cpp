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

#include "engines/effects/kernels.h"
#include "util/fixedpoint.h"

namespace deluge::effects {

bool SendAndVolume::process(std::span<dsp::StereoSample> block, std::span<int32_t> reverb,
                            const SendAndVolumeParams& params) {

	int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(params.post_fx_volume, params.reverb_send) << 5;

	int32_t postFXAndReverbVolumeL, postFXAndReverbVolumeR;
	int32_t amplitudeIncrementL = 0;
	int32_t amplitudeIncrementR = 0;
	postFXAndReverbVolumeL = postFXAndReverbVolumeR =
	    (multiply_32x32_rshift32(params.post_reverb_volume, params.post_fx_volume) << 5);

	// The amplitude increment applies to the post-FX volume. We want to have it just so that we can respond better to
	// sidechain volume ducking, which is done through post-FX volume.
	if (params.ramp) {
		auto postReverbSendVolumeIncrement =
		    (int32_t)((double)(params.post_reverb_volume - last_post_reverb_volume_) / (double)block.size());
		amplitudeIncrementL = amplitudeIncrementR =
		    (multiply_32x32_rshift32(params.post_fx_volume, postReverbSendVolumeIncrement) << 5);
	}

	if (params.pan != 0 && params.stereo) {
		// Set up panning
		const PanAmplitudes amplitudes = pan_amplitudes(params.pan);
		const int32_t amplitudeL = amplitudes.left;
		const int32_t amplitudeR = amplitudes.right;

		postFXAndReverbVolumeL = multiply_32x32_rshift32(postFXAndReverbVolumeL, amplitudeL) << 2;
		postFXAndReverbVolumeR = multiply_32x32_rshift32(postFXAndReverbVolumeR, amplitudeR) << 2;

		amplitudeIncrementL = multiply_32x32_rshift32(amplitudeIncrementL, amplitudeL) << 2;
		amplitudeIncrementR = multiply_32x32_rshift32(amplitudeIncrementR, amplitudeR) << 2;
	}

	auto reverbOut = reverb.begin();
	for (dsp::StereoSample& sample : block) {
		// Send to reverb
		if (params.reverb_send != 0) {
			*(reverbOut++) += multiply_32x32_rshift32(sample.l + sample.r, reverbSendAmountAndPostFXVolume) << 1;
		}

		if (params.ramp) {
			postFXAndReverbVolumeL += amplitudeIncrementL;
			postFXAndReverbVolumeR += amplitudeIncrementR;
		}

		// Apply post-fx and post-reverb-send volume
		sample.l = multiply_32x32_rshift32(sample.l, postFXAndReverbVolumeL) << 5;
		sample.r = multiply_32x32_rshift32(sample.r, postFXAndReverbVolumeR) << 5;
	}

	last_post_reverb_volume_ = params.post_reverb_volume;
	return params.reverb_send != 0;
}

} // namespace deluge::effects
