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
#include "synth/sources/dx7/dx7_source.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/stereo_sample.h"
#include <span>

namespace deluge::synth {

SourceRenderResult DX7Source::render(const SourceBlock& block) {
	// The port renders the FM block (a pooled dx7::Voice + log-pitch/control derivation); the bespoke per-unison
	// amplitude+sum below is Voice's buffer layout. An empty span means the voice has ended.
	std::span<const int32_t> fm = dx_->render(block.num_samples, block.phase_increment, block.dx7.amp_mod_param,
	                                          block.dx7.patch_changed, block.dx7.note_code);
	if (fm.empty()) {
		return SourceRenderResult::UNASSIGN;
	}

	using dsp::Q;
	// The DX7's sample is Q24, full scale 2^24, and mixes in at a quarter of the buffer's Q31 full scale: its word is
	// read as Q26. The source amplitude is a Q31 gain, stepped before each sample; the buffer's sums wrap.
	Q<31> amplitude = Q<31>::from_raw(block.source_amplitude);
	const Q<31> step = Q<31>::from_raw(block.amplitude_increment);
	if (block.stereo_unison) {
		const Q<30> amplitude_l = Q<30>::from_raw(block.amplitude_l);
		const Q<30> amplitude_r = Q<30>::from_raw(block.amplitude_r);
		for (int i = 0; i < block.num_samples; i++) {
			amplitude = add_wrap(amplitude, step);
			const auto amplified = mul_as_wrap<Q<31>>(Q<26>::from_raw(fm[i]), amplitude);
			const dsp::StereoSample panned = dsp::panned_mono(amplified.raw(), amplitude_l, amplitude_r);
			auto* frame = &block.osc_buffer[i << 1];
			frame[0] = add_wrap(Q<31>::from_raw(frame[0]), Q<31>::from_raw(panned.l)).raw();
			frame[1] = add_wrap(Q<31>::from_raw(frame[1]), Q<31>::from_raw(panned.r)).raw();
		}
	}
	else {
		for (int i = 0; i < block.num_samples; i++) {
			amplitude = add_wrap(amplitude, step);
			const auto amplified = mul_as_wrap<Q<31>>(Q<26>::from_raw(fm[i]), amplitude);
			block.osc_buffer[i] = add_wrap(Q<31>::from_raw(block.osc_buffer[i]), amplified).raw();
		}
	}
	return SourceRenderResult::ACTIVE;
}

} // namespace deluge::synth
