/*
 * Copyright © 2026 Mark Adams
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
#include "synth/sources/supersaw/supersaw_source.h"
#include "dsp-next/fixed/bandlimited/band_limited_table.h" // get_table_number
#include "dsp-next/fixed/vca/amplitude.h"

namespace deluge::synth {

void SupersawSource::render_into(const BlockControls& controls, uint32_t phase_increment, int32_t wave_index_last,
                                 int32_t wave_index_increment, deluge::dsp::fixed::Buffer output,
                                 deluge::dsp::fixed::Buffer scratch) {
	using deluge::dsp::fixed::Supersaw;
	const size_t num_samples = output.size();

	// The plain saw's dire-CPU fallback: skip the band limiting for lower notes
	const bool band_limit = deluge::dsp::fixed::get_table_number(phase_increment).first >= controls.cpu_direness + 6;
	const int32_t spread = wave_index_last + wave_index_increment * static_cast<int32_t>(num_samples);

	int32_t* dst = controls.apply_amplitude ? scratch.data() : output.data();
	Supersaw::render(deluge::dsp::fixed::Buffer{dst, num_samples}, phase_increment, spread,
	                 Supersaw::num_saws_from_pulse_width(controls.pulse_width), centre_phase_, side_phases_,
	                 band_limit);

	if (controls.apply_amplitude) {
		// The supersaw comes out at a quarter of a saw's level; out = scratch*amp + out.
		deluge::dsp::fixed::Amplitude vca{controls.amplitude << 2};
		vca.render_block(deluge::dsp::fixed::Signal{scratch.data(), num_samples}, deluge::dsp::fixed::Signal{output},
		                 output);
	}
}

} // namespace deluge::synth
