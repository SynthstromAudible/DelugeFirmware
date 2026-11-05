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

void SupersawSource::render_into(const BlockControls& controls, dsp::UQ<32> phase_increment, int32_t wave_index_last,
                                 int32_t wave_index_increment, std::span<dsp::Q<31>> output,
                                 std::span<dsp::Q<31>> scratch) {
	using deluge::dsp::fixed::Supersaw;
	const size_t num_samples = output.size();

	// The plain saw's dire-CPU fallback: skip the band limiting for lower notes
	// The band ladder and the kernel step the increment as a raw word
	const bool band_limit =
	    deluge::dsp::fixed::get_table_number(phase_increment.raw()).first >= controls.cpu_direness + 6;
	const int32_t spread = wave_index_last + wave_index_increment * static_cast<int32_t>(num_samples);

	dsp::Q<31>* dst = controls.apply_amplitude ? scratch.data() : output.data();
	Supersaw::render(std::span<dsp::Q<31>>{dst, num_samples}, phase_increment.raw(), spread,
	                 Supersaw::num_saws_from_pulse_width(controls.pulse_width), centre_phase_, side_phases_,
	                 band_limit);

	if (controls.apply_amplitude) {
		// The supersaw comes out at a quarter of a saw's level; out = scratch*amp + out.
		deluge::dsp::fixed::Amplitude vca{scale_shl_wrap<2>(controls.amplitude)};
		vca.render_block(std::span<const dsp::Q<31>>{scratch.data(), num_samples}, output, output);
	}
}

} // namespace deluge::synth
