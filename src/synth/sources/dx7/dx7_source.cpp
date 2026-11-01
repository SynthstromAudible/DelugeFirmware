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
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32
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

	int32_t source_amplitude_now = block.source_amplitude;
	if (block.stereo_unison) {
		for (int i = 0; i < block.num_samples; i++) {
			source_amplitude_now += block.amplitude_increment;
			int amplified = multiply_32x32_rshift32(fm[i], source_amplitude_now) << 6;
			block.osc_buffer[(i << 1)] += multiply_32x32_rshift32(amplified, block.amplitude_l) << 2;
			block.osc_buffer[(i << 1) + 1] += multiply_32x32_rshift32(amplified, block.amplitude_r) << 2;
		}
	}
	else {
		for (int i = 0; i < block.num_samples; i++) {
			source_amplitude_now += block.amplitude_increment;
			block.osc_buffer[i] += multiply_32x32_rshift32(fm[i], source_amplitude_now) << 6;
		}
	}
	return SourceRenderResult::ACTIVE;
}

} // namespace deluge::synth
