/*
 * Copyright © 2023-2026 Mark Adams, Synthstrom Audible Limited
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

#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"

namespace deluge::dsp::fixed {

RmsFeedbackCompressor::View RmsFeedbackCompressor::view() const {
	const Compressor::Coefficients& c = dsp_.coefficients();
	const Compressor::State& s = dsp_.state();
	return {
	    .configured = configured_,
	    .attack_rate = c.attack_rate,
	    .release_rate = c.release_rate,
	    .fraction = c.fraction,
	    .makeup = s.makeup,
	    // From the knob, not the kernel's: the kernel takes the knob only when the next block's levels arrive.
	    .threshold = Compressor::threshold_fraction(threshold_knob_),
	    .detector_moveability = c.detector_moveability,
	    .reduction_envelope = s.reduction_envelope,
	    .current_volume_l = s.loop.current_volume_l,
	    .current_volume_r = s.loop.current_volume_r,
	    .log_rms = s.log_rms,
	    .mean = s.mean,
	    .saturation_drive = Compressor::kSaturator.drive,
	    .saturation_l = s.loop.saturation_l,
	    .saturation_r = s.loop.saturation_r,
	    .seeded = s.seeded,
	    .detector_l = s.loop.detector_l,
	    .detector_r = s.loop.detector_r,
	    .attack_ms = c.attack_ms,
	    .release_ms = c.release_ms,
	    .base_gain = c.base_gain,
	    .threshold_knob = threshold_knob_,
	    .dry = c.dry,
	    .wet = c.wet,
	    .gain_reduction = s.gain_reduction,
	};
}

void RmsFeedbackCompressor::configure(const CompressorKnobs& knobs) {
	if (knobs == configured_) {
		return;
	}
	if (knobs.threshold != configured_.threshold) {
		threshold_knob_ = knobs.threshold;
	}
	configured_ = knobs;
	dsp_.configure(settings());
}

void RmsFeedbackCompressor::setBaseGain(float base_gain) {
	base_gain_ = base_gain;
	dsp_.configure(settings());
}

void RmsFeedbackCompressor::render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume) {
	// The kernel's block arithmetic divides by the block's length.
	if (block.empty()) {
		return;
	}
	dsp_.on<&Compressor::set_levels>(Compressor::Levels{
	    .volume_l = volume_l, .volume_r = volume_r, .final_volume = final_volume, .threshold = threshold_knob_});
	if (!dsp_.state().seeded) {
		// The first frame interpolates against the block's own start rather than against silence.
		dsp_.on<&Compressor::seed>(block.front());
	}
	dsp_.renderBlock(block, block);
	// The level the next block's gain follows.
	dsp_.on<&Compressor::finish_level>();
}

} // namespace deluge::dsp::fixed
