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
#include "synth/source/supersaw_source.h"
#include "dsp-next/fixed/bandlimited/band_limited_table.h" // getTableNumber
#include "dsp-next/fixed/vca/amplitude.h"

namespace deluge::synth {

void SupersawSource::renderInto(const BlockControls& controls, uint32_t phaseIncrement, int32_t waveIndexLast,
                                int32_t waveIndexIncrement, deluge::dsp::fixed::Buffer output,
                                deluge::dsp::fixed::Buffer scratch) {
	using deluge::dsp::fixed::Supersaw;
	const size_t numSamples = output.size();

	// The plain saw's dire-CPU fallback: skip the band limiting for lower notes
	const bool bandLimit = deluge::dsp::fixed::getTableNumber(phaseIncrement).first >= controls.cpu_direness + 6;
	const int32_t spread = waveIndexLast + waveIndexIncrement * static_cast<int32_t>(numSamples);

	int32_t* dst = controls.apply_amplitude ? scratch.data() : output.data();
	Supersaw::render(deluge::dsp::fixed::Buffer{dst, numSamples}, phaseIncrement, spread,
	                 Supersaw::numSawsFromPulseWidth(controls.pulse_width), centre_phase_, side_phases_, bandLimit);

	if (controls.apply_amplitude) {
		// The supersaw comes out at a quarter of a saw's level; out = scratch*amp + out.
		deluge::dsp::fixed::Amplitude vca{controls.amplitude << 2};
		vca.renderBlock(deluge::dsp::fixed::Signal{scratch.data(), numSamples}, deluge::dsp::fixed::Signal{output},
		                output);
	}
}

} // namespace deluge::synth
